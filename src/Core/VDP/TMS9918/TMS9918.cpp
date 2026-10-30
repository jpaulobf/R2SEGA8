#include "TMS9918.h"
#include <stdexcept>

namespace R2SEGA8::Core
{
    namespace
    {
        // Aproximação RGB fixa da paleta NTSC; a cor zero revela o backdrop, sem vídeo externo.
        constexpr std::uint8_t Palette[16][3] = {{0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120}, {84, 85, 237}, {125, 118, 252}, {212, 82, 77}, {66, 235, 245}, {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128}, {33, 176, 59}, {201, 91, 186}, {204, 204, 204}, {255, 255, 255}};
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
        state_.evaluatedSprites.fill({});
        state_.nextSprites.fill({});
        state_.sprites.fill({});
        state_.selectedSprites = state_.nextSpriteCount = state_.spriteCount = 0;
        state_.extraFetchIndex = 4;
        state_.extraFetchPhase = 0;
        state_.spriteTargetLine = 0;
        state_.spriteEvaluationStopped = true;
        state_.evaluationSpriteLimit = state_.limitSpritesPerLine;
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

    void TMS9918::evaluateSprite(unsigned index)
    {
        if (state_.spriteEvaluationStopped || (state_.registers[1] & 0x50) != 0x40)
            return;
        const unsigned base = (state_.registers[5] & 0x7f) << 7;
        const unsigned y = state_.vram[base + index * 4];
        if (!(state_.status & 0x40))
            state_.status = static_cast<std::uint8_t>((state_.status & 0xe0) | index);
        if (y == 0xd0)
        {
            state_.spriteEvaluationStopped = true;
            return;
        }
        const int top = (y > 0xe0 ? static_cast<int>(y) - 256 : static_cast<int>(y)) + 1;
        const unsigned size = (state_.registers[1] & 2) ? 16 : 8, scale = (state_.registers[1] & 1) ? 2 : 1;
        const int row = static_cast<int>(state_.spriteTargetLine) - top;
        if (row < 0 || row >= static_cast<int>(size * scale))
            return;
        if (state_.selectedSprites == 4)
        {
            // 5S independe da opção visual: informa o primeiro quinto sprite e não gera INT.
            if (!(state_.status & 0xc0))
                state_.status = static_cast<std::uint8_t>((state_.status & 0xa0) | 0x40 | index);
            if (state_.evaluationSpriteLimit)
            {
                state_.spriteEvaluationStopped = true;
                return;
            }
        }
        state_.evaluatedSprites[state_.selectedSprites++].index = static_cast<std::uint8_t>(index);
    }

    void TMS9918::fetchSprite(Sprite &sprite, unsigned phase, unsigned targetLine)
    {
        const auto &r = state_.registers;
        const unsigned entry = ((r[5] & 0x7f) << 7) + sprite.index * 4;
        switch (phase)
        {
        case 0:
        {
            sprite.y = state_.vram[entry];
            sprite.size = (r[1] & 2) ? 16 : 8;
            sprite.scale = (r[1] & 1) ? 2 : 1;
            const int top = (sprite.y > 0xe0 ? int(sprite.y) - 256 : int(sprite.y)) + 1;
            const int row = static_cast<int>(targetLine) - top;
            sprite.visible = sprite.y != 0xd0 && row >= 0 && row < sprite.size * sprite.scale;
            sprite.row = sprite.visible ? static_cast<std::uint8_t>(row / sprite.scale) : 0;
            break;
        }
        case 1:
            sprite.x = state_.vram[entry + 1];
            break;
        case 2:
            sprite.name = state_.vram[entry + 2];
            break;
        case 3:
            sprite.attributes = state_.vram[entry + 3];
            break;
        default:
        {
            // SIZE=1 ignora os dois bits baixos do nome; os quadrantes são A/B à esquerda, C/D à direita.
            const unsigned name = sprite.name & (sprite.size == 16 ? 0xfc : 0xff);
            const unsigned address = ((r[6] & 7) << 11) + name * 8 + sprite.row + (phase == 5 ? 16 : 0);
            const auto pattern = state_.vram[address & 0x3fff];
            if (phase == 4)
                sprite.left = pattern;
            else
                sprite.right = pattern;
            break;
        }
        }
    }

    void TMS9918::clockSprites()
    {
        const unsigned dot = state_.dot;
        if (!dot)
        {
            state_.sprites = state_.nextSprites;
            state_.spriteCount = state_.nextSpriteCount;
        }
        // Coordenadas do diagrama de temporização convertidas: dot 0 = ciclo 63 (primeiro pixel ativo).
        // A seleção começa na margem anterior, continua durante a imagem e alimenta a linha seguinte.
        if (dot == 316)
        {
            state_.spriteTargetLine = (state_.line + 2) % LinesPerFrame;
            state_.selectedSprites = 0;
            state_.extraFetchIndex = 4;
            state_.extraFetchPhase = 0;
            state_.evaluatedSprites.fill({});
            state_.evaluationSpriteLimit = state_.limitSpritesPerLine;
            state_.spriteEvaluationStopped = state_.spriteTargetLine >= Height;
        }
        if (dot >= 316 && dot <= 330 && !(dot & 1))
            evaluateSprite((dot - 316) / 2);
        else if (dot <= 240)
        {
            // Os primeiros oito Y são consecutivos; os demais cedem um slot de cada quatro à CPU.
            const unsigned group = dot / 32, slot = dot % 32;
            if (slot == 0 || slot == 8 || slot == 16)
                evaluateSprite(8 + group * 3 + slot / 8);
        }

        // Extensão opcional: os sprites 5..32 usam um fetcher lógico separado, um byte por dot.
        // Ele não ocupa slots físicos adicionais nem modifica a cadência dos quatro sprites originais.
        if (!state_.evaluationSpriteLimit && state_.extraFetchIndex < state_.selectedSprites)
        {
            fetchSprite(state_.evaluatedSprites[state_.extraFetchIndex], state_.extraFetchPhase, state_.spriteTargetLine);
            if (++state_.extraFetchPhase == 6)
            {
                state_.extraFetchPhase = 0;
                ++state_.extraFetchIndex;
            }
        }

        const unsigned target = (state_.line + 1) % LinesPerFrame;
        if (dot == 250)
        {
            state_.nextSprites = state_.evaluatedSprites;
            state_.nextSpriteCount = target < Height && state_.spriteTargetLine == target ? state_.selectedSprites : 0;
        }
        // Y/X/nome/cor/padrão esquerdo/direito são buscados separadamente, inclusive o segundo padrão de SIZE=0.
        constexpr unsigned FetchDots[4][6] = {
            {250, 252, 254, 256, 258, 260}, {262, 264, 266, 268, 270, 272}, {274, 276, 288, 290, 292, 294}, {296, 298, 300, 302, 304, 306}};
        for (unsigned index = 0; index < 4 && index < state_.nextSpriteCount; ++index)
            for (unsigned phase = 0; phase < 6; ++phase)
                if (dot == FetchDots[index][phase])
                    fetchSprite(state_.nextSprites[index], phase, target);
    }

    std::uint8_t TMS9918::spriteColor()
    {
        unsigned overlaps = 0;
        std::uint8_t color = 0;
        for (unsigned index = 0; index < state_.spriteCount; ++index)
        {
            const auto &sprite = state_.sprites[index];
            if (!sprite.visible)
                continue;
            const int left = sprite.x - ((sprite.attributes & 0x80) ? 32 : 0);
            const int offset = static_cast<int>(state_.dot) - left;
            if (offset < 0 || offset >= sprite.size * sprite.scale)
                continue;
            const unsigned column = static_cast<unsigned>(offset) / sprite.scale;
            const auto pattern = column < 8 ? sprite.left : sprite.right;
            if (!(pattern & (0x80 >> (column & 7))))
                continue;
            ++overlaps; // A cor transparente participa da colisão, mas não esconde o próximo sprite.
            if (!color)
                color = sprite.attributes & 15;
        }
        if (overlaps > 1)
            state_.status |= 0x20;
        return color;
    }

    void TMS9918::drawPixel()
    {
        const auto backdrop = state_.registers[7] & 15;
        unsigned color = (state_.registers[1] & 0x40) ? backgroundColor() : backdrop;
        if (!color)
            color = backdrop;
        if ((state_.registers[1] & 0x50) == 0x40)
        {
            const auto sprite = spriteColor();
            if (sprite)
                color = sprite;
        }
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
        clockSprites();
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
        if (state.selectedSprites > 32 || state.nextSpriteCount > 32 || state.spriteCount > 32 || state.extraFetchIndex < 4 ||
            state.extraFetchIndex > 32 || state.extraFetchPhase > 5 || state.spriteTargetLine >= LinesPerFrame)
            throw std::invalid_argument("Invalid TMS9918 sprite pipeline state.");
        for (const auto *bank : {&state.evaluatedSprites, &state.nextSprites, &state.sprites})
            for (const auto &sprite : *bank)
                if (sprite.index > 31 || sprite.row > 15 || (sprite.size != 8 && sprite.size != 16) ||
                    (sprite.scale != 1 && sprite.scale != 2))
                    throw std::invalid_argument("Invalid TMS9918 sprite fetch state.");
        state_ = state;
    }

    VideoFrame TMS9918::frame() const
    {
        return {state_.images[state_.drawingImage ^ 1].data(), Width, Height, Width * 4, state_.completedFrames};
    }
}