#pragma once
#include "Core/Board.h"
#include "Core/Audio/SN76489/SN76489.h"
#include "Core/IO/ControllerInput.h"
#include "Core/VDP/SegaVDP/SegaVDP.h"
#include <array>

namespace R2SEGA8::Core
{
    // Perfil inicial do mapeador Sega; periféricos e outros mapeadores estão pendentes.
    class MasterSystem final : public Board
    {
    public:
        // Aceita ROMs de até 1 MiB e aloca 8 KiB de RAM interna zerada.
        explicit MasterSystem(Core::Cartridge cartridge, bool pal = false);

        bool hasMemoryMap() const override { return true; }
        unsigned clockFrequency() const override { return pal_ ? 3546893 : 3579545; }
        unsigned cyclesPerFrame() const override { return SegaVDP::DotsPerLine * 2 * vdp_.linesPerFrame() / 3; }
        std::optional<Common::ConsoleProfile> consoleProfile() const override
        {
            return pal_ ? Common::ConsoleProfile::PAL : Common::ConsoleProfile::NTSC;
        }
        VideoFrame videoFrame() const override { return vdp_.frame(); }
        void setRenderLayers(bool sprites, bool tiles) override
        {
            vdp_.setRenderSprites(sprites);
            vdp_.setRenderTiles(tiles);
        }
        void setSpriteUnlimited(bool enabled) override { vdp_.setSpriteUnlimited(enabled); }
        void setButtons(unsigned player, Common::ButtonMask buttons) override;
        void setInput(const Common::InputState &buttons) override;
        void takeAudio(std::vector<float> &samples) override { psg_.takeSamples(samples); }
        void setAudioSettings(const Common::AudioSettings &settings) override { psg_.setAudioSettings(settings); }

        // Lê ROM bancária, RAM de cartucho habilitada ou RAM interna espelhada.
        std::uint8_t readMemory(std::uint16_t address) const override;

        // Escreve RAM e atualiza os registradores do mapeador em FFFC–FFFF.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;

        // Conecta o contador vertical, as portas do VDP e as leituras dos dois controles.
        std::uint8_t input(std::uint16_t port) override;
        void output(std::uint16_t port, std::uint8_t value) override;
        void clock(const Z80Pins &) override;
        const SegaVDP &vdp() const { return vdp_; }
        const SN76489 &psg() const { return psg_; }

    private:
        bool pal_;
        std::array<std::uint8_t, 3> banks_{{0, 1, 2}};
        std::uint8_t mapperControl_ = 0;
        std::size_t bankMask_ = 0;
        SegaVDP vdp_;
        SN76489 psg_;
        IO::ControllerInput controls_;
    };
}