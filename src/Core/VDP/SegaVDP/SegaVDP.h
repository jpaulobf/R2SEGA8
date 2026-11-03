#pragma once

#include <array>
#include <cstdint>
#include "../VideoFrame.h"

namespace R2SEGA8::Core
{
    // Perfil 315-5124 (SMS1): portas, raster NTSC/PAL e Mode 4 de 192 linhas.
    class SegaVDP
    {
    public:
        static constexpr unsigned Width = 256, Height = 192;
        static constexpr unsigned DotsPerLine = 342, LinesPerFrame = 262, PalLinesPerFrame = 313;
        static constexpr unsigned MasterTicksPerFrame = DotsPerLine * 2 * LinesPerFrame;
        static constexpr unsigned FrameBytes = Width * Height * 4;
        struct State
        {
            std::array<std::uint8_t, 0x4000> vram{};
            std::array<std::uint8_t, 0x20> cram{};
            std::array<std::uint8_t, 16> registers{};
            std::uint16_t address = 0;
            std::uint8_t code = 0, readBuffer = 0, status = 0;
            bool secondControlByte = false;
            std::uint16_t dot = 0, line = 0;
            std::uint8_t masterPhase = 0, drawingImage = 0;
            std::uint8_t horizontalScroll = 0, verticalScroll = 0;
            std::uint8_t lineCounter = 0;
            bool lineInterruptPending = false, frameInterruptPending = false;
            std::uint64_t completedFrames = 0;
            template <class Archive> void stateArchive(Archive &ar)
            {
                ar(vram, cram, registers, address, code, readBuffer, status, secondControlByte, dot, line,
                   masterPhase, drawingImage, horizontalScroll, verticalScroll, lineCounter,
                   lineInterruptPending, frameInterruptPending, completedFrames);
            }
        };

        struct SpriteLine
        {
            int x = 0;
            std::array<std::uint8_t, 4> planes{};
            bool doubleWidth = false;
            template <class Archive> void state(Archive &ar) { ar(x, planes, doubleWidth); }
        };
        struct SelectedSprite
        {
            std::uint8_t index = 0, row = 0;
            template <class Archive> void state(Archive &ar) { ar(index, row); }
        };
        struct Snapshot
        {
            State chip;
            std::array<SelectedSprite, 64> selectedSprites{};
            unsigned selectedSpriteCount = 0;
            bool selectedSpriteOverflow = false, spriteSelectionValid = false;
            std::array<SpriteLine, 64> sprites{};
            unsigned spriteCount = 0, hardwareSpriteCount = 0;
            std::array<std::array<std::uint8_t, FrameBytes>, 2> images{};
            template <class Archive> void stateArchive(Archive &ar)
            {
                chip.stateArchive(ar);
                ar(selectedSprites, selectedSpriteCount, selectedSpriteOverflow, spriteSelectionValid,
                   sprites, spriteCount, hardwareSpriteCount, images);
            }
        };

        explicit SegaVDP(bool pal = false) : pal_(pal) {}
        unsigned linesPerFrame() const { return pal_ ? PalLinesPerFrame : LinesPerFrame; }

        // A VRAM e CRAM inicializam em zero por convenção; os acessos são imediatos nesta etapa.
        std::uint8_t readData();
        void writeData(std::uint8_t value);
        std::uint8_t readStatus();
        void writeControl(std::uint8_t value);
        void runMasterTicks(unsigned count);
        // Contador vertical NTSC 192 linhas: 00–DA, D5–FF; leitura sem efeitos colaterais.
        std::uint8_t verticalCounter() const
        {
            // A mudança ocorre no apagamento horizontal, antes do próximo pixel visível.
            const unsigned line = (state_.line + (state_.dot > HBlankEventDot + 2 ? 1 : 0)) % linesPerFrame();
            return static_cast<std::uint8_t>(pal_ ? (line <= 0xf2 ? line : line - 57)
                                                  : (line <= 0xda ? line : line - 6));
        }
        bool interrupt() const
        {
            return (state_.frameInterruptPending && (state_.registers[1] & 0x20)) ||
                   (state_.lineInterruptPending && (state_.registers[0] & 0x10));
        }
        VideoFrame frame() const;
        void setRenderTiles(bool enabled) { renderTiles_ = enabled; }
        void setRenderSprites(bool enabled) { renderSprites_ = enabled; }
        void setSpriteUnlimited(bool enabled) { spriteUnlimited_ = enabled; }

        // Permite validar a interface sem alterar latches ou memórias.
        const State &state() const { return state_; }
        void snapshot(Snapshot &snapshot) const;
        void restore(const Snapshot &snapshot);

    private:
        // HClock -18, em relação ao início dos 256 pixels ativos.
        static constexpr unsigned HBlankEventDot = DotsPerLine - 18;
        bool pal_ = false;
        State state_{};
        std::array<SelectedSprite, 64> selectedSprites_{};
        unsigned selectedSpriteCount_ = 0;
        bool selectedSpriteOverflow_ = false, spriteSelectionValid_ = false;
        std::array<SpriteLine, 64> sprites_{};
        unsigned spriteCount_ = 0, hardwareSpriteCount_ = 0;
        std::array<std::array<std::uint8_t, FrameBytes>, 2> images_{};
        bool renderTiles_ = true, renderSprites_ = true, spriteUnlimited_ = true;
        void incrementAddress();
        void drawPixel();
        void selectSprites(unsigned line);
        void prepareSprites();
    };
}