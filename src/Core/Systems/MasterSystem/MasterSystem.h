#pragma once
#include "Core/Board.h"
#include "Core/Cartridge/SegaMapper.h"
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

        // Informa que o mapa de memória da placa está disponível.
        bool hasMemoryMap() const override { return true; }

        // Informa que a placa oferece salvamento e restauração de estado.
        bool supportsSaveState() const override { return true; }

        // Serializa CPU, memória, mapeador, vídeo, áudio e controles.
        std::vector<std::uint8_t> saveState() const override;

        // Valida e restaura um estado compatível com a ROM e o perfil atuais.
        void loadState(const std::vector<std::uint8_t> &data) override;

        // Retorna o clock da CPU para o perfil NTSC ou PAL selecionado.
        unsigned clockFrequency() const override { return pal_ ? 3546893 : 3579545; }

        // Retorna os T-states correspondentes ao quadro completo do VDP.
        unsigned cyclesPerFrame() const override { return SegaVDP::DotsPerLine * 2 * vdp_.linesPerFrame() / 3; }

        // Informa o perfil de vídeo efetivo da placa.
        std::optional<Common::ConsoleProfile> consoleProfile() const override
        {
            return pal_ ? Common::ConsoleProfile::PAL : Common::ConsoleProfile::NTSC;
        }

        // Retorna o último quadro completo do VDP.
        VideoFrame videoFrame() const override { return vdp_.frame(); }

        // Controla a apresentação dos sprites e do fundo.
        void setRenderLayers(bool sprites, bool tiles) override
        {
            vdp_.setRenderSprites(sprites);
            vdp_.setRenderTiles(tiles);
        }

        // Configura o limite de sprites na apresentação do VDP.
        void setSpriteUnlimited(bool enabled) override { vdp_.setSpriteUnlimited(enabled); }

        // Atualiza os botões lógicos de um jogador.
        void setButtons(unsigned player, Common::ButtonMask buttons) override;

        // Atualiza os controles e a linha NMI do botão Pause.
        void setInput(const Common::InputState &buttons) override;

        void setLightPhaser(bool enabled, int x, int y, bool trigger)
        {
            phaserEnabled_ = enabled;
            phaserTrigger_ = enabled && trigger;
            vdp_.setLightPhaserPosition(enabled ? x : -1, enabled ? y : -1);
        }

        // Transfere as amostras mono produzidas desde a última coleta.
        void takeAudio(std::vector<float> &samples) override { psg_.takeSamples(samples); }

        // Aplica as preferências de volume e ativação ao PSG.
        void setAudioSettings(const Common::AudioSettings &settings) override { psg_.setAudioSettings(settings); }

        // Lê ROM bancária, RAM de cartucho habilitada ou RAM interna espelhada.
        std::uint8_t readMemory(std::uint16_t address) const override;

        // Escreve RAM e atualiza os registradores do mapeador em FFFC–FFFF.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;

        // Conecta o contador vertical, as portas do VDP e as leituras dos dois controles.
        std::uint8_t input(std::uint16_t port) override;

        // Encaminha escritas às portas de áudio e vídeo.
        void output(std::uint16_t port, std::uint8_t value) override;

        // Avança os periféricos e atualiza a linha de interrupção da CPU.
        void clock(const Z80Pins &) override;

        // Expõe o VDP para inspeção sem permitir sua alteração.
        const SegaVDP &vdp() const { return vdp_; }

        // Expõe o PSG para inspeção sem permitir sua alteração.
        const SN76489 &psg() const { return psg_; }

    private:
        bool pal_;
        SegaMapper mapper_;
        SegaVDP vdp_;
        SN76489 psg_;
        IO::ControllerInput controls_;
        std::uint8_t ioControl_ = 0xff;
        std::uint8_t horizontalLatch_ = 0;
        bool phaserEnabled_ = false, phaserTrigger_ = false;
    };
}