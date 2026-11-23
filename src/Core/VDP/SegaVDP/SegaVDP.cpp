#include "SegaVDP.h"
#include <stdexcept>

namespace R2SEGA8::Core
{
    namespace
    {
        // Equivalentes na CRAM da paleta fixa medida no VDP 315-5124.
        constexpr std::array<std::uint8_t, 16> smsGraphicsIPalette = {
            0x00, 0x00, 0x08, 0x0c, 0x10, 0x30, 0x01, 0x3c,
            0x02, 0x03, 0x05, 0x0f, 0x04, 0x33, 0x15, 0x3f};
        constexpr unsigned graphicsISpriteLimit = 4;
    }

    void SegaVDP::snapshot(Snapshot &s) const
    {
        s.chip = state_;
        s.selectedSprites = selectedSprites_;
        s.selectedSpriteCount = selectedSpriteCount_;
        s.selectedSpriteOverflow = selectedSpriteOverflow_;
        s.spriteSelectionValid = spriteSelectionValid_;
        s.sprites = sprites_;
        s.spriteCount = spriteCount_;
        s.hardwareSpriteCount = hardwareSpriteCount_;
        s.images = images_;
        s.gameGearCram = gameGearCram_;
        s.gameGearCramLatch = gameGearCramLatch_;
    }

    void SegaVDP::restore(const Snapshot &s)
    {
        if (s.chip.address >= 0x4000 || s.chip.code > 3 || s.chip.dot >= DotsPerLine ||
            s.chip.line >= linesPerFrame() || s.chip.masterPhase > 1 || s.chip.drawingImage > 1 ||
            s.selectedSpriteCount > 64 || s.spriteCount > 64 || s.hardwareSpriteCount > 8 ||
            s.hardwareSpriteCount > s.spriteCount)
            throw std::invalid_argument("Invalid Sega VDP state.");
        for (const auto &sprite : s.selectedSprites)
            if (sprite.index >= 64 || sprite.row >= 32)
                throw std::invalid_argument("Invalid Sega VDP sprite selection.");
        for (const auto &sprite : s.sprites)
            if (sprite.x < -32 || sprite.x >= static_cast<int>(Width))
                throw std::invalid_argument("Invalid Sega VDP sprite position.");
        if (model_ == Model::GameGear)
            for (std::size_t i = 1; i < s.gameGearCram.size(); i += 2)
                if (s.gameGearCram[i] > 15)
                    throw std::invalid_argument("Invalid Game Gear palette state.");
        state_ = s.chip;
        selectedSprites_ = s.selectedSprites;
        selectedSpriteCount_ = s.selectedSpriteCount;
        selectedSpriteOverflow_ = s.selectedSpriteOverflow;
        spriteSelectionValid_ = s.spriteSelectionValid;
        sprites_ = s.sprites;
        spriteCount_ = s.spriteCount;
        hardwareSpriteCount_ = s.hardwareSpriteCount;
        images_ = s.images;
        gameGearCram_ = s.gameGearCram;
        gameGearCramLatch_ = s.gameGearCramLatch;
        prepareSpritePixels();
    }

    bool SegaVDP::graphicsI() const
    {
        return !(state_.registers[0] & 0x06) && !(state_.registers[1] & 0x18);
    }

    unsigned SegaVDP::graphicsIColor(unsigned x, unsigned y) const
    {
        const auto &r = state_.registers;
        const unsigned name = state_.vram[((r[2] & 15) << 10) + (y / 8) * 32 + x / 8];
        const auto pattern = state_.vram[((r[4] & 7) << 11) + name * 8 + (y & 7)];
        const auto colors = state_.vram[(r[3] << 6) + name / 8];
        return pattern & (0x80 >> (x & 7)) ? colors >> 4 : colors & 15;
    }

    void SegaVDP::selectSprites(unsigned line)
    {
        selectedSpriteCount_ = 0;
        selectedSpriteOverflow_ = false;
        spriteSelectionValid_ = true;
        const bool legacy = graphicsI();
        // A seleção continua com a tela apagada: o overflow independe de BLK.
        if (line >= Height || (!legacy && !(state_.registers[0] & 0x04)))
            return;
        const unsigned base = (state_.registers[5] & (legacy ? 0x7f : 0x7e)) << 7;
        const unsigned height = (state_.registers[1] & 2) ? 16 : 8;
        const unsigned scale = (state_.registers[1] & 1) ? 2 : 1;
        const unsigned entries = legacy ? 32 : 64;
        const unsigned limit = legacy ? graphicsISpriteLimit : 8;
        for (unsigned index = 0; index < entries; ++index)
        {
            const auto y = state_.vram[base + index * (legacy ? 4 : 1)];
            if (y == 0xd0)
                break;
            const unsigned row = (line - y - 1) & 0xff;
            if (row >= height * scale)
                continue;
            const SelectedSprite selected{static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(row)};
            if (selectedSpriteCount_ == limit)
            {
                selectedSpriteOverflow_ = true;
                if (!spriteUnlimited_)
                {
                    if (legacy)
                        selectedSprites_[limit] = selected; // Índice do quinto sprite, sem desenhá-lo.
                    break;
                }
            }
            selectedSprites_[selectedSpriteCount_++] = selected;
        }
    }

    void SegaVDP::prepareSprites()
    {
        spriteCount_ = hardwareSpriteCount_ = 0;
        if (graphicsI())
        {
            prepareGraphicsISprites();
            prepareSpritePixels();
            return;
        }
        if (!(state_.registers[0] & 0x04))
        {
            prepareSpritePixels();
            return;
        }
        const unsigned base = (state_.registers[5] & 0x7e) << 7;
        const unsigned height = (state_.registers[1] & 2) ? 16 : 8;
        const unsigned scale = (state_.registers[1] & 1) ? 2 : 1;
        for (unsigned i = 0; i < selectedSpriteCount_; ++i)
        {
            const unsigned index = selectedSprites_[i].index;
            // No 315-5124, R5 bit 0 mascara A7 das leituras de X e padrão.
            const auto attribute = (base + ((model_ == Model::GameGear || (state_.registers[5] & 1)) ? 0x80 : 0) + index * 2) & 0x3fff;
            auto &sprite = sprites_[spriteCount_++];
            sprite.x = state_.vram[attribute] - ((state_.registers[0] & 8) ? 8 : 0);
            unsigned tile = state_.vram[(attribute + 1) & 0x3fff];
            if (height == 16)
                tile &= ~1u;
            tile |= (state_.registers[6] & 4) << 6;
            const unsigned tileRow = selectedSprites_[i].row / scale;
            const unsigned address = (tile * 32 + tileRow * 4) &
                                     (model_ == Model::GameGear ? 0x3fff : (0x27ff | ((state_.registers[6] & 3) << 11)));
            for (unsigned plane = 0; plane < 4; ++plane)
                sprite.planes[plane] = state_.vram[(address + plane) & 0x3fff];
        }
        hardwareSpriteCount_ = spriteCount_ < 8 ? spriteCount_ : 8;
        // Perfil SMS1: os últimos quatro sprites selecionados só ampliam verticalmente.
        for (unsigned i = 0; i < spriteCount_; ++i)
            sprites_[i].doubleWidth = scale == 2 && (model_ == Model::GameGear || i >= 8 || i + 4 < hardwareSpriteCount_);
        prepareSpritePixels();
    }

    void SegaVDP::prepareGraphicsISprites()
    {
        const auto &r = state_.registers;
        const unsigned attributes = (r[5] & 0x7f) << 7;
        const unsigned patterns = (r[6] & 7) << 11;
        const bool large = (r[1] & 2) != 0;
        const unsigned scale = (r[1] & 1) ? 2 : 1;
        const auto halfWidth = static_cast<int>(8 * scale);
        for (unsigned i = 0; i < selectedSpriteCount_ && i < 32; ++i)
        {
            const auto &selection = selectedSprites_[i];
            const unsigned entry = attributes + (selection.index & 31) * 4;
            const auto flags = state_.vram[entry + 3];
            const unsigned color = flags & 15;
            if (!color)
                continue;
            int x = state_.vram[entry + 1] - ((flags & 0x80) ? 32 : 0);
            const unsigned name = state_.vram[entry + 2] & (large ? 0xfc : 0xff);
            const unsigned row = selection.row / scale;
            // Cada metade de oito pixels cabe no mesmo formato planar usado pelo Mode 4.
            for (unsigned half = 0; half < (large ? 2u : 1u) && x < static_cast<int>(Width); ++half, x += halfWidth)
            {
                const auto pattern = state_.vram[(patterns + name * 8 + row + half * 16) & 0x3fff];
                auto &sprite = sprites_[spriteCount_++];
                sprite.x = x;
                sprite.doubleWidth = scale == 2;
                for (unsigned plane = 0; plane < 4; ++plane)
                    sprite.planes[plane] = color & (1u << plane) ? pattern : 0;
                if (i < graphicsISpriteLimit)
                    ++hardwareSpriteCount_;
            }
        }
    }

    void SegaVDP::prepareSpritePixels()
    {
        spritePixels_.fill(0);
        spriteOverlaps_.fill(0);
        for (unsigned i = 0; i < spriteCount_; ++i)
        {
            const auto &sprite = sprites_[i];
            const unsigned width = sprite.doubleWidth ? 16 : 8;
            for (unsigned offset = 0; offset < width; ++offset)
            {
                const int x = sprite.x + static_cast<int>(offset);
                if (x < 0 || x >= static_cast<int>(Width))
                    continue;
                const unsigned bit = 7 - offset / (sprite.doubleWidth ? 2 : 1);
                unsigned color = 0;
                for (unsigned plane = 0; plane < 4; ++plane)
                    color |= ((sprite.planes[plane] >> bit) & 1) << plane;
                if (!color)
                    continue;
                if (!spritePixels_[x])
                    spritePixels_[x] = static_cast<std::uint8_t>(color);
                if (i < hardwareSpriteCount_ && spriteOverlaps_[x] < 2)
                    ++spriteOverlaps_[x];
            }
        }
    }

    void SegaVDP::drawPixel()
    {
        const auto x = state_.dot, y = state_.line;
        const bool legacy = graphicsI();
        const bool mode4 = (state_.registers[0] & 0x04) != 0;
        const bool display = (state_.registers[1] & 0x40) != 0;
        const bool maskedColumn = mode4 && (state_.registers[0] & 0x20) && x < 8;
        const unsigned backdrop = state_.registers[7] & 15;
        unsigned paletteIndex = (legacy ? 0 : 16) + backdrop;
        bool backgroundPriority = false;
        if (legacy && display && renderTiles_)
        {
            const auto color = graphicsIColor(x, y);
            if (color)
                paletteIndex = color;
        }
        else if (mode4 && display && renderTiles_ && !maskedColumn)
        {
            // R8 desloca o fundo para a direita; R9 move o fundo para cima.
            const unsigned scrollX = (state_.registers[0] & 0x40) && y < 16 ? 0 : state_.horizontalScroll;
            // O scroll fino deixa uma faixa inicial na cor zero da paleta de fundo.
            if (x < (scrollX & 7))
                paletteIndex = 0;
            else
            {
                const unsigned scrollY = (state_.registers[0] & 0x80) && x >= 192 ? 0 : state_.verticalScroll;
                const unsigned tileX = (x + 256 - scrollX) & 0xff;
                const unsigned tileY = (y + scrollY) % 224;
                const unsigned nameBase = (state_.registers[2] & 0x0e) << 10;
                const unsigned nameMask = model_ == Model::GameGear ? 0x3fff : 0x3bff | ((state_.registers[2] & 1) << 10);
                const unsigned nameAddress = (nameBase + ((tileY / 8) * 32 + tileX / 8) * 2) & nameMask;
                const unsigned entry = state_.vram[nameAddress] | (state_.vram[(nameAddress + 1) & 0x3fff] << 8);
                const unsigned row = (entry & 0x0400) ? 7 - (tileY & 7) : tileY & 7;
                const unsigned column = (entry & 0x0200) ? tileX & 7 : 7 - (tileX & 7);
                const unsigned pattern = (entry & 0x1ff) * 32 + row * 4;
                unsigned color = 0;
                for (unsigned plane = 0; plane < 4; ++plane)
                {
                    // R3 mascara A6–A13 nos planos 0/1; R4 mascara A11–A13 nos planos 2/3.
                    const unsigned mask = model_ == Model::GameGear ? 0x3fff : (plane < 2 ? (0x003f | (state_.registers[3] << 6)) : (0x07ff | ((state_.registers[4] & 7) << 11)));
                    color |= ((state_.vram[(pattern + plane) & mask] >> column) & 1) << plane;
                }
                paletteIndex = ((entry & 0x0800) ? 16 : 0) + color;
                backgroundPriority = (entry & 0x1000) && color;
            }
        }
        if ((legacy || mode4) && display && renderSprites_ && spritePixels_[x] && !backgroundPriority && !maskedColumn)
            paletteIndex = (legacy ? 0 : 16) + spritePixels_[x];
        auto &image = images_[state_.drawingImage];
        const auto offset = (y * Width + x) * 4;
        if (model_ == Model::GameGear)
        {
            if (legacy)
                paletteIndex += 16;
            const auto low = gameGearCram_[paletteIndex * 2], high = gameGearCram_[paletteIndex * 2 + 1];
            image[offset] = (low & 15) * 17;
            image[offset + 1] = (low >> 4) * 17;
            image[offset + 2] = (high & 15) * 17;
        }
        else
        {
            const auto color = legacy ? smsGraphicsIPalette[paletteIndex] : state_.cram[paletteIndex];
            image[offset] = (color & 3) * 85;
            image[offset + 1] = ((color >> 2) & 3) * 85;
            image[offset + 2] = ((color >> 4) & 3) * 85;
        }
        image[offset + 3] = 255;
        const int dx = static_cast<int>(x) - phaserX_, dy = static_cast<int>(y) - phaserY_;
        const bool light = phaserX_ >= 0 && phaserY_ >= 0 && dx >= -5 && dx <= 5 && dy >= -5 && dy <= 5 &&
                           dx * dx + dy * dy <= 25 &&
                           (image[offset] * 299 + image[offset + 1] * 587 + image[offset + 2] * 114) >= 160000;
        if (light && !phaserLight_)
        {
            // O sinal óptico chega depois do pixel; jogos calibram o contador
            // incluindo esse atraso e o deslocamento horizontal do vídeo.
            phaserLatch_ = static_cast<std::uint8_t>(horizontalCounter() + 0x16);
            phaserLatchPending_ = true;
        }
        // Persiste por parte da linha, como o sensor, para que o Z80 consiga
        // amostrar o pulso entre leituras consecutivas da porta TH.
        if (light)
            phaserPulse_ = 64;
        phaserLight_ = phaserPulse_ != 0;
    }

    void SegaVDP::detectSpriteCollision(unsigned x)
    {
        const bool mode4 = (state_.registers[0] & 0x04) != 0;
        if (!(state_.registers[1] & 0x40) || (!mode4 && !graphicsI()) ||
            (mode4 && (state_.registers[0] & 0x20) && x < 8))
            return;
        if (spriteOverlaps_[x] > 1)
            state_.status |= 0x20;
    }

    void SegaVDP::runMasterTicks(unsigned count)
    {
        while (count--)
        {
            state_.masterPhase ^= 1;
            if (state_.masterPhase)
                continue;
            if (phaserPulse_ && --phaserPulse_ == 0)
                phaserLight_ = false;
            if (state_.dot == 0 && state_.line < Height)
            {
                // O primeiro quadro não tem uma linha anterior para capturar R8.
                if (state_.line == 0 && state_.completedFrames == 0)
                    state_.horizontalScroll = state_.registers[8];
                if (state_.line == 0)
                    state_.verticalScroll = state_.registers[9];
                if (!spriteSelectionValid_)
                    selectSprites(state_.line); // Inicialização antes da primeira linha emulada.
                prepareSprites();
            }
            if (state_.line < Height && state_.dot < Width)
                drawPixel();
            // O latch de colisão segue o processamento do pixel com atraso;
            // continua na borda direita para incluir o último pixel ativo.
            const unsigned collisionDelay = model_ == Model::MasterSystem ? 10 : 0;
            if (state_.line < Height && state_.dot >= collisionDelay && state_.dot < Width + collisionDelay)
                detectSpriteCollision(state_.dot - collisionDelay);
            if (state_.dot == Width)
                // A busca é aproximada ao fim da linha; o hardware a distribui pela linha.
                selectSprites((state_.line + 1) % linesPerFrame());
            // O raster usa a origem do pixel ativo. Os instantes do Master System
            // foram comparados às leituras do SMS VDP Test.
            if (state_.dot == HBlankEventDot)
            {
                state_.horizontalScroll = state_.registers[8];
            }
            if (state_.line == Height)
            {
                if (state_.dot == HBlankEventDot - 1)
                    state_.status |= 0x80;
                if (model_ == Model::MasterSystem)
                {
                    // Uma leitura de status pode reconhecer o VBlank antes da asserção de INT.
                    if (state_.dot == HBlankEventDot && (state_.status & 0x80))
                        state_.frameInterruptPending = true;
                }
                else if (state_.dot == HBlankEventDot - 2)
                    state_.frameInterruptPending = true;
            }
            if (state_.dot == HBlankEventDot + 1)
            {
                const unsigned nextLine = (state_.line + 1) % linesPerFrame();
                if (nextLine <= Height)
                {
                    if (state_.lineCounter == 0)
                    {
                        state_.lineCounter = state_.registers[10];
                        state_.lineInterruptPending = true;
                    }
                    else
                        --state_.lineCounter;
                }
                else
                    state_.lineCounter = state_.registers[10];
            }
            if (state_.dot == HBlankEventDot - 1 && selectedSpriteOverflow_)
            {
                if (graphicsI() && !(state_.status & 0x40))
                    state_.status = (state_.status & 0xe0) | (selectedSprites_[graphicsISpriteLimit].index & 31);
                state_.status |= 0x40;
            }
            if (++state_.dot == DotsPerLine)
            {
                state_.dot = 0;
                if (++state_.line == Height)
                {
                    state_.drawingImage ^= 1;
                    ++state_.completedFrames;
                }
                else if (state_.line == linesPerFrame())
                    state_.line = 0;
            }
        }
    }

    VideoFrame SegaVDP::frame() const
    {
        const auto *pixels = images_[state_.drawingImage ^ 1].data();
        // A área do portátil começa em (48, 24); o passo continua sendo o da imagem completa.
        if (model_ == Model::GameGear)
            return {pixels + (24 * Width + 48) * 4, 160, 144, Width * 4, state_.completedFrames};
        return {pixels, Width, Height, Width * 4, state_.completedFrames};
    }

    void SegaVDP::incrementAddress()
    {
        state_.address = (state_.address + 1) & 0x3fff;
    }

    std::uint8_t SegaVDP::readData()
    {
        state_.secondControlByte = false;
        const auto result = state_.readBuffer;
        state_.readBuffer = state_.vram[state_.address];
        incrementAddress();
        return result;
    }

    void SegaVDP::writeData(std::uint8_t value)
    {
        state_.secondControlByte = false;
        if (state_.code == 3 && model_ == Model::GameGear)
        {
            // No Game Gear, a escrita par alimenta um latch; a ímpar grava os dois bytes da cor.
            const auto address = state_.address & 0x3f;
            if (!(address & 1))
                gameGearCramLatch_ = value;
            else
            {
                gameGearCram_[address - 1] = gameGearCramLatch_;
                gameGearCram_[address] = value & 0x0f;
            }
        }
        else if (state_.code == 3)
            state_.cram[state_.address & 0x1f] = value & 0x3f;
        else
            state_.vram[state_.address] = value;
        state_.readBuffer = value;
        incrementAddress();
    }

    std::uint8_t SegaVDP::readStatus()
    {
        state_.secondControlByte = false;
        const auto result = state_.status;
        state_.status = 0;
        state_.lineInterruptPending = false;
        state_.frameInterruptPending = false;
        return result;
    }

    void SegaVDP::writeControl(std::uint8_t value)
    {
        if (!state_.secondControlByte)
        {
            state_.address = (state_.address & 0x3f00) | value;
            state_.secondControlByte = true;
            return;
        }
        state_.secondControlByte = false;
        state_.address = (state_.address & 0xff) | ((value & 0x3f) << 8);
        state_.code = value >> 6;
        if (state_.code == 0)
        {
            state_.readBuffer = state_.vram[state_.address];
            incrementAddress();
        }
        else if (state_.code == 2)
        {
            const bool wasGraphicsI = graphicsI();
            state_.registers[value & 0x0f] = static_cast<std::uint8_t>(state_.address);
            if (wasGraphicsI != graphicsI())
            {
                // A seleção anterior pertence a outra organização da tabela de sprites.
                selectedSpriteCount_ = spriteCount_ = hardwareSpriteCount_ = 0;
                selectedSpriteOverflow_ = spriteSelectionValid_ = false;
                prepareSpritePixels();
            }
        }
    }
}