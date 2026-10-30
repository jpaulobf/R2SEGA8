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
        state_.background.fill({});
        state_.outputMode = state_.outputStart = state_.outputWidth = 0;
        state_.outputLine = 0;
        state_.outputValid = false;
        state_.cpuAccessPending = state_.cpuAccessWrite = false;
        state_.cpuAccessData = state_.cpuAccessDelay = state_.refreshRow = 0;
        state_.access = Access::None;
        state_.accessAddress = state_.accessData = 0;
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
        state_.evaluationSpriteUnlimited = state_.spriteUnlimited;
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
        requestCPUAccess(false, 0);
        return value;
    }

    void TMS9918::writeData(std::uint8_t value)
    {
        state_.secondControlByte = false;
        state_.readBuffer = value;
        requestCPUAccess(true, value);
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
        if (value & 0x80)
        {
            // No TMS99xxA, um incremento ocorrido entre os bytes também altera a parte baixa do ponteiro.
            state_.address = static_cast<std::uint16_t>(((value & 0x3f) << 8) | (state_.address & 0xff));
            state_.registers[value & 7] = state_.controlLatch;
        }
        else
        {
            state_.address = static_cast<std::uint16_t>(((value & 0x3f) << 8) | state_.controlLatch);
            if (!(value & 0x40))
                requestCPUAccess(false, 0); // A pré-busca aguarda preparação e um slot livre.
        }
    }

    bool TMS9918::interrupt() const
    {
        return (state_.status & 0x80) && (state_.registers[1] & 0x20);
    }

    void TMS9918::requestCPUAccess(bool writing, std::uint8_t value)
    {
        // O novo pedido troca tipo/dado, mas não reinicia a preparação do pedido já retido.
        if (!state_.cpuAccessPending)
            state_.cpuAccessDelay = CPUAccessDelay;
        state_.cpuAccessPending = true;
        state_.cpuAccessWrite = writing;
        state_.cpuAccessData = value;
    }

    unsigned TMS9918::mode() const
    {
        return (state_.registers[0] & 2) | ((state_.registers[1] & 0x10) >> 4) | ((state_.registers[1] & 8) >> 1);
    }

    bool TMS9918::spritesEnabled() const
    {
        const auto videoMode = mode();
        return (state_.registers[1] & 0x50) == 0x40 && (videoMode == 0 || videoMode == 2 || videoMode == 4);
    }

    std::uint8_t TMS9918::readVRAM(unsigned address, Access owner)
    {
        state_.access = owner;
        state_.accessAddress = static_cast<std::uint16_t>(address & 0x3fff);
        state_.accessData = state_.vram[state_.accessAddress];
        return state_.accessData;
    }

    bool TMS9918::cpuSlot(unsigned cycle, unsigned videoMode, bool active) const
    {
        if (!(cycle & 1))
            return false;
        if (!active)
            return !(cycle >= 53 && cycle <= 305 && (cycle - 53) % 4 == 0);
        if (videoMode == 1)
            return !((cycle >= 59 && cycle <= 293 && (cycle - 59) % 6 == 0) || (cycle >= 63 && cycle <= 297 && (cycle - 63) % 6 == 0));
        const bool graphicsSlot = cycle <= 7 || (cycle >= 29 && cycle <= 35) || (cycle >= 55 && cycle <= 279 && (cycle - 55) % 32 == 0) ||
                                  cycle == 309 || cycle == 311 || cycle == 341;
        return graphicsSlot || (videoMode == 4 && cycle >= 57 && cycle <= 305 && (cycle - 57) % 8 == 0);
    }

    bool TMS9918::fetchBackground(unsigned cycle, unsigned videoMode)
    {
        unsigned column = 0, phase = 0;
        if (videoMode == 1)
        {
            if (cycle >= 59 && cycle <= 293 && (cycle - 59) % 6 == 0)
                column = (cycle - 59) / 6;
            else if (cycle >= 63 && cycle <= 297 && (cycle - 63) % 6 == 0)
            {
                column = (cycle - 63) / 6;
                phase = 2;
            }
            else
                return false;
        }
        else
        {
            if (cycle < 53 || cycle > 307)
                return false;
            column = (cycle - 53) / 8;
            const unsigned offset = (cycle - 53) % 8;
            if (offset == 0)
                phase = 0;
            else if (offset == 4 && videoMode != 4)
                phase = 1;
            else if (offset == 6)
                phase = 2;
            else
                return false;
        }
        const unsigned line = (state_.line + (state_.dot >= 332 ? 1 : 0)) % LinesPerFrame;
        if (line >= Height)
            return true;
        auto &cell = state_.background[column & 1];
        const auto &r = state_.registers;
        if (phase == 0)
        {
            cell = {};
            cell.column = static_cast<std::uint8_t>(column);
            cell.line = static_cast<std::uint16_t>(line);
            cell.mode = static_cast<std::uint8_t>(videoMode);
            cell.name = readVRAM(((r[2] & 15) << 10) + (line / 8) * (videoMode == 1 ? 40 : 32) + column, Access::Name);
            cell.named = true;
            return true;
        }
        if (!cell.named || cell.line != line || cell.column != column || cell.mode != videoMode)
            return true; // Mudanças de modo não inventam um nome que ainda não foi buscado.
        const unsigned offset = ((line & 0xc0) << 5) | (cell.name << 3) | (line & 7);
        const unsigned colorMask = ((r[3] & 0x7f) << 6) | 0x3f;
        if (phase == 1)
        {
            const unsigned address = videoMode == 2 ? ((r[3] & 0x80) << 6) | (offset & colorMask) : (r[3] << 6) + cell.name / 8;
            cell.colors = readVRAM(address, Access::Color);
        }
        else
        {
            const unsigned patternMask = ((r[4] & 3) << 11) | (colorMask & 0x7ff);
            const unsigned address = videoMode == 2 ? ((r[4] & 4) << 11) | (offset & patternMask)
                                                    : ((r[4] & 7) << 11) + cell.name * 8 + (videoMode == 4 ? (line / 4) & 7 : line & 7);
            const auto value = readVRAM(address, Access::Pattern);
            if (videoMode == 4)
                cell.colors = value;
            else
                cell.pattern = value;
            cell.ready = true;
        }
        return true;
    }

    void TMS9918::clockMemory()
    {
        state_.access = Access::None;
        const unsigned videoMode = mode(), cycle = (state_.dot + 63) % DotsPerLine;
        const bool enabled = (state_.registers[1] & 0x40) && (videoMode == 0 || videoMode == 1 || videoMode == 2 || videoMode == 4);
        // A margem anterior já busca a primeira célula e os sprites antes da primeira linha visível.
        const bool active = enabled && (state_.line < Height || state_.line == LinesPerFrame - 1 ||
                                        (state_.line == LinesPerFrame - 2 && state_.dot >= 316));
        clockSprites();
        if (state_.access != Access::None)
            return; // Um slot pertence a um único acesso físico, mesmo durante mudanças de modo.
        if (state_.dot & 1)
            return;
        if (active && fetchBackground(cycle, videoMode))
            return;
        if (!active && cycle >= 53 && cycle <= 305 && (cycle - 53) % 4 == 0)
        {
            state_.access = Access::Refresh;
            state_.accessAddress = state_.refreshRow;
            state_.refreshRow = (state_.refreshRow + 1) & 0x7f;
            return;
        }
        if (!cpuSlot(cycle, videoMode, active) || !state_.cpuAccessPending || state_.cpuAccessDelay)
            return;
        if (state_.cpuAccessWrite)
        {
            state_.access = Access::CPUWrite;
            state_.accessAddress = state_.address;
            state_.accessData = state_.cpuAccessData;
            state_.vram[state_.address] = state_.cpuAccessData;
        }
        else
            state_.readBuffer = readVRAM(state_.address, Access::CPURead);
        incrementAddress();
        state_.cpuAccessPending = false;
    }

    std::uint8_t TMS9918::backgroundColor()
    {
        const unsigned videoMode = mode(), x = state_.dot;
        // O formato faz parte da célula carregada: trocar GI por Multicolor não reinterpreta seus bytes.
        const bool continuing = state_.outputValid && state_.outputLine == state_.line && x >= state_.outputStart &&
                                x < state_.outputStart + state_.outputWidth;
        if (!continuing)
        {
            state_.outputValid = false;
            const bool supported = videoMode == 0 || videoMode == 1 || videoMode == 2 || videoMode == 4;
            const unsigned start = videoMode == 1 ? 6 : 0, width = videoMode == 1 ? 6 : 8;
            const bool inside = videoMode != 1 || (x >= 6 && x < 246);
            if (supported && inside && (x - start) % width == 0)
            {
                const unsigned column = (x - start) / width;
                const auto &cell = state_.background[column & 1];
                if (cell.ready && cell.line == state_.line && cell.column == column && cell.mode == videoMode)
                {
                    state_.pattern = cell.pattern;
                    state_.colors = cell.colors;
                    state_.outputMode = static_cast<std::uint8_t>(videoMode);
                    state_.outputStart = static_cast<std::uint8_t>(x);
                    state_.outputWidth = static_cast<std::uint8_t>(width);
                    state_.outputLine = state_.line;
                    state_.outputValid = true;
                }
            }
        }
        if (!state_.outputValid)
            return state_.registers[7] & 15;
        const unsigned pixel = x - state_.outputStart;
        if (state_.outputMode == 1)
            return (state_.pattern & (0x80 >> pixel)) ? state_.registers[7] >> 4 : state_.registers[7] & 15;
        const bool foreground = state_.outputMode == 4 ? pixel < 4 : (state_.pattern & (0x80 >> pixel)) != 0;
        return foreground ? state_.colors >> 4 : state_.colors & 15;
    }

    void TMS9918::evaluateSprite(unsigned index)
    {
        if (state_.spriteEvaluationStopped || !spritesEnabled())
            return;
        const unsigned base = (state_.registers[5] & 0x7f) << 7;
        const unsigned y = readVRAM(base + index * 4, Access::SpriteY);
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
            if (!state_.evaluationSpriteUnlimited)
            {
                state_.spriteEvaluationStopped = true;
                return;
            }
        }
        // Estados restaurados podem combinar uma seleção cheia com uma fase anterior da avaliação.
        // Mesmo nesse caso, a extensão sem limite continua restrita aos 32 sprites da SAT.
        if (state_.selectedSprites >= state_.evaluatedSprites.size())
        {
            state_.spriteEvaluationStopped = true;
            return;
        }
        state_.evaluatedSprites[state_.selectedSprites++].index = static_cast<std::uint8_t>(index);
    }

    void TMS9918::fetchSprite(Sprite &sprite, unsigned phase, unsigned targetLine, bool physical)
    {
        const auto &r = state_.registers;
        const unsigned entry = ((r[5] & 0x7f) << 7) + sprite.index * 4;
        // O fetch extra da opção ilimitada não ocupa o barramento físico.
        const auto read = [&](unsigned address, Access owner)
        {
            return physical ? readVRAM(address, owner) : state_.vram[address & 0x3fff];
        };
        switch (phase)
        {
        case 0:
        {
            sprite.y = read(entry, Access::SpriteAttribute);
            sprite.size = (r[1] & 2) ? 16 : 8;
            sprite.scale = (r[1] & 1) ? 2 : 1;
            const int top = (sprite.y > 0xe0 ? int(sprite.y) - 256 : int(sprite.y)) + 1;
            const int row = static_cast<int>(targetLine) - top;
            sprite.visible = sprite.y != 0xd0 && row >= 0 && row < sprite.size * sprite.scale;
            sprite.row = sprite.visible ? static_cast<std::uint8_t>(row / sprite.scale) : 0;
            break;
        }
        case 1:
            sprite.x = read(entry + 1, Access::SpriteAttribute);
            break;
        case 2:
            sprite.name = read(entry + 2, Access::SpriteAttribute);
            break;
        case 3:
            sprite.attributes = read(entry + 3, Access::SpriteAttribute);
            break;
        default:
        {
            // SIZE=1 ignora os dois bits baixos do nome; os quadrantes são A/B à esquerda, C/D à direita.
            const unsigned name = sprite.name & (sprite.size == 16 ? 0xfc : 0xff);
            const unsigned address = ((r[6] & 7) << 11) + name * 8 + sprite.row + (phase == 5 ? 16 : 0);
            const auto pattern = read(address, Access::SpritePattern);
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
            state_.evaluationSpriteUnlimited = state_.spriteUnlimited;
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
        if (spritesEnabled() && state_.evaluationSpriteUnlimited && state_.extraFetchIndex < state_.selectedSprites)
        {
            fetchSprite(state_.evaluatedSprites[state_.extraFetchIndex], state_.extraFetchPhase, state_.spriteTargetLine, false);
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
        for (unsigned index = 0; spritesEnabled() && index < 4 && index < state_.nextSpriteCount; ++index)
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
        const auto background = backgroundColor(); // Os latches continuam avançando mesmo com a saída apagada.
        unsigned color = (state_.registers[1] & 0x40) ? background : backdrop;
        if (!color)
            color = backdrop;
        if (spritesEnabled())
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
        if (state_.cpuAccessDelay)
            --state_.cpuAccessDelay;
        state_.masterPhase ^= 1;
        if (state_.masterPhase)
            return;
        clockMemory();
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
        if (state.cpuAccessDelay > CPUAccessDelay || state.refreshRow > 127 || state.accessAddress >= 0x4000 ||
            static_cast<unsigned>(state.access) > static_cast<unsigned>(Access::SpritePattern))
            throw std::invalid_argument("Invalid TMS9918 memory sequencer state.");
        if (state.outputLine >= LinesPerFrame || (state.outputWidth != 0 && state.outputWidth != 6 && state.outputWidth != 8) ||
            state.outputStart + state.outputWidth > Width ||
            (state.outputValid &&
             (!state.outputWidth || (state.outputMode != 0 && state.outputMode != 1 && state.outputMode != 2 && state.outputMode != 4))))
            throw std::invalid_argument("Invalid TMS9918 background output latch.");
        for (const auto &cell : state.background)
            if (cell.line >= LinesPerFrame || cell.column > 39 || cell.mode > 7)
                throw std::invalid_argument("Invalid TMS9918 background fetch state.");
        state_ = state;
    }

    VideoFrame TMS9918::frame() const
    {
        return {state_.images[state_.drawingImage ^ 1].data(), Width, Height, Width * 4, state_.completedFrames};
    }
}