#include "TMS9918.h"
#include <stdexcept>

namespace R2SEGA8::Core
{
    namespace
    {
        // Aproximação RGB fixa da paleta NTSC; a cor zero revela o backdrop, sem vídeo externo.
        constexpr std::uint8_t Palette[16][3] = {{0, 0, 0},     {0, 0, 0},       {33, 200, 66},   {94, 220, 120},
                                                 {84, 85, 237}, {125, 118, 252}, {212, 82, 77},   {66, 235, 245},
                                                 {252, 85, 84}, {255, 121, 120}, {212, 193, 84},  {230, 206, 128},
                                                 {33, 176, 59}, {201, 91, 186},  {204, 204, 204}, {255, 255, 255}};
    } // namespace

    TMS9918::TMS9918()
    {
        reset();
    }

    void TMS9918::reset()
    {
        state_.registers.fill(0);
        state_.address = state_.dot = state_.line = 0;
        state_.readBuffer = state_.controlLatch = 0;
        state_.status = 0x1f;
        state_.masterPhase = state_.drawingImage = state_.pattern = state_.colors = 0;
        state_.secondControlByte = false;
        state_.masterTicks = state_.completedFrames = 0;
        for (auto &image : state_.images)
        {
            image.fill(0);
            for (unsigned i = 3; i < FrameBytes; i += 4)
                image[i] = 255;
        }
    }

    void TMS9918::incrementAddress()
    {
        state_.address = (state_.address + 1) & 0x3fff;
    }

    std::uint8_t TMS9918::readData()
    {
        state_.secondControlByte = false;
        const auto value = state_.readBuffer;
        state_.readBuffer = state_.vram[state_.address];
        incrementAddress();
        return value;
    }

    void TMS9918::writeData(std::uint8_t value)
    {
        state_.secondControlByte = false;
        state_.vram[state_.address] = value;
        state_.readBuffer = value;
        incrementAddress();
    }

    std::uint8_t TMS9918::readStatus()
    {
        const auto value = state_.status;
        state_.status &= 0x1f;
        state_.secondControlByte = false;
        return value;
    }

    void TMS9918::writeControl(std::uint8_t value)
    {
        if (!state_.secondControlByte)
        {
            state_.controlLatch = value;
            state_.address = (state_.address & 0x3f00) | value;
            state_.secondControlByte = true;
            return;
        }
        state_.secondControlByte = false;
        state_.address = static_cast<std::uint16_t>(((value & 0x3f) << 8) | state_.controlLatch);
        if (value & 0x80)
            state_.registers[value & 7] = state_.controlLatch;
        else if (!(value & 0x40))
        {
            // O comando de leitura já faz a primeira pré-busca; o primeiro IN retorna esse byte.
            state_.readBuffer = state_.vram[state_.address];
            incrementAddress();
        }
    }

    bool TMS9918::interrupt() const
    {
        return (state_.status & 0x80) && (state_.registers[1] & 0x20);
    }

    std::uint8_t TMS9918::backgroundColor()
    {
        const auto &r = state_.registers;
        const unsigned mode = (r[0] & 2) | ((r[1] & 0x10) >> 4) | ((r[1] & 8) >> 1);
        const unsigned x = state_.dot, y = state_.line;
        const unsigned nameBase = (r[2] & 15) << 10;
        if (mode == 1) // Text: 40 células de seis pixels, deslocadas seis pixels em relação a Graphics I.
        {
            if (x < 6 || x >= 246)
                return r[7] & 15;
            const unsigned column = (x - 6) / 6, pixel = (x - 6) % 6;
            if (!pixel)
            {
                const auto name = state_.vram[(nameBase + (y / 8) * 40 + column) & 0x3fff];
                state_.pattern = state_.vram[(((r[4] & 7) << 11) + name * 8 + (y & 7)) & 0x3fff];
            }
            // Os dois bits inferiores do padrão não participam do caractere de seis pixels.
            return (state_.pattern & (0x80 >> pixel)) ? r[7] >> 4 : r[7] & 15;
        }
        if (mode != 0 && mode != 2 && mode != 4)
            return r[7] & 15; // Combinações não documentadas continuam fora deste perfil.
        if (!(x & 7))
        {
            const auto name = state_.vram[(nameBase + (y / 8) * 32 + x / 8) & 0x3fff];
            if (mode == 2) // Graphics II: padrões e cores por linha, em três blocos de 64 linhas.
            {
                const unsigned offset = ((y & 0xc0) << 5) | (name << 3) | (y & 7);
                const unsigned colorMask = ((r[3] & 0x7f) << 6) | 0x3f;
                // Nos TMS99xxA, os bits baixos da máscara de cores também limitam os padrões.
                const unsigned patternMask = ((r[4] & 3) << 11) | (colorMask & 0x7ff);
                state_.pattern = state_.vram[((r[4] & 4) << 11) | (offset & patternMask)];
                state_.colors = state_.vram[((r[3] & 0x80) << 6) | (offset & colorMask)];
            }
            else if (mode == 4) // Multicolor: um byte define dois blocos horizontais de quatro pixels.
                state_.colors = state_.vram[(((r[4] & 7) << 11) + name * 8 + ((y / 4) & 7)) & 0x3fff];
            else
            {
                state_.pattern = state_.vram[(((r[4] & 7) << 11) + name * 8 + (y & 7)) & 0x3fff];
                state_.colors = state_.vram[((r[3] << 6) + name / 8) & 0x3fff];
            }
        }
        const bool foreground = mode == 4 ? !(x & 4) : (state_.pattern & (0x80 >> (x & 7))) != 0;
        return foreground ? state_.colors >> 4 : state_.colors & 15;
    }

    void TMS9918::drawPixel()
    {
        const auto backdrop = state_.registers[7] & 15;
        unsigned color = (state_.registers[1] & 0x40) ? backgroundColor() : backdrop;
        if (!color)
            color = backdrop;
        auto &image = state_.images[state_.drawingImage];
        const unsigned offset = (state_.line * Width + state_.dot) * 4;
        image[offset] = Palette[color][0];
        image[offset + 1] = Palette[color][1];
        image[offset + 2] = Palette[color][2];
        image[offset + 3] = 255;
    }

    void TMS9918::tick()
    {
        ++state_.masterTicks;
        state_.masterPhase ^= 1;
        if (state_.masterPhase)
            return;
        if (state_.line < Height && state_.dot < Width)
            drawPixel();
        if (++state_.dot == DotsPerLine)
        {
            state_.dot = 0;
            if (++state_.line == Height)
            {
                // Publica um quadro estável antes de iniciar a próxima área ativa.
                state_.status |= 0x80;
                state_.drawingImage ^= 1;
                ++state_.completedFrames;
            }
            else if (state_.line == LinesPerFrame)
                state_.line = 0;
        }
    }

    void TMS9918::runMasterTicks(std::uint64_t count)
    {
        for (std::uint64_t i = 0; i < count; ++i)
            tick();
    }

    void TMS9918::restore(const State &state)
    {
        if (state.address >= 0x4000 || state.dot >= DotsPerLine || state.line >= LinesPerFrame || state.masterPhase > 1 ||
            state.drawingImage > 1)
            throw std::invalid_argument("Invalid TMS9918 state.");
        state_ = state;
    }

    VideoFrame TMS9918::frame() const
    {
        return {state_.images[state_.drawingImage ^ 1].data(), Width, Height, Width * 4, state_.completedFrames};
    }
} // namespace R2SEGA8::Core
