#pragma once
#include "Core/Audio/SN76489/SN76489.h"
#include "Core/IO/Joypad.h"
#include "Core/VDP/TMS9918/TMS9918.h"
#include "Core/Board.h"

namespace R2SEGA8::Core
{
    // Representa o SG-1000 com ROM simples ou RAM de cartucho The Castle e 1 KiB de RAM interna espelhada.
    // Cartuchos com seleção de bancos ainda não são implementados.
    class SG1000 final : public Board
    {
    public:
        // Recebe o cartucho, aloca 1 KiB de RAM e rejeita ROMs maiores que 48 KiB.
        explicit SG1000(Core::Cartridge cartridge);

        // Informa que o perfil de ROM simples possui um mapa de memória implementado.
        bool hasMemoryMap() const override { return true; }
        bool supportsSaveState() const override { return true; }
        std::vector<std::uint8_t> saveState() const override;
        void loadState(const std::vector<std::uint8_t> &data) override;

        // Perfil inicial de clock NTSC; o perfil PAL acompanhará a implementação do vídeo.
        unsigned clockFrequency() const override { return 3579545; }

        // NTSC: 342 dots × 262 linhas × 2 clocks do cristal, com três clocks por T-state.
        unsigned cyclesPerFrame() const override { return Core::TMS9918::MasterTicksPerFrame / 3; }

        // Lê a ROM em 0000–BFFF ou a RAM interna espelhada em C000–FFFF.
        // No perfil The Castle, 8000–9FFF acessa a RAM do cartucho e A000–BFFF espelha a ROM.
        // Retorna provisoriamente FF quando não há um byte de ROM no endereço.
        std::uint8_t readMemory(std::uint16_t address) const override;

        // Escreve na RAM interna ou na RAM do cartucho, conforme o perfil; ignora escritas em ROM.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;

        // Atualiza as intenções do jogador, resolve o turbo pelo quadro emulado e aplica Pause/Hold à NMI.
        void setButtons(unsigned player, Common::ButtonMask buttons) override;

        // Atualiza os dois jogadores juntos para não criar uma borda NMI artificial ao trocar a fonte de Start.
        void setInput(const Common::InputState &buttons) override;

        // Encaminha 80–BF ao VDP e C0–FF aos controles; portas restantes retornam FF provisoriamente.
        std::uint8_t input(std::uint16_t port) override;

        // Encaminha 40–7F ao PSG e 80–BF ao VDP; ignora o byte alto das portas.
        void output(std::uint16_t port, std::uint8_t value) override;

        // Avança VDP e PSG por T-state, atualiza INT e liga READY do PSG à entrada WAIT.
        void clock(const Core::Z80Pins &pins) override;

        // Retorna o último quadro completo, sem expor o VDP à camada de apresentação.
        Core::VideoFrame videoFrame() const override { return vdp_.frame(); }

        // Consulta o VDP para depuração e validação, sem alterar latches ou sinais.
        const Core::TMS9918 &vdp() const { return vdp_; }

        // Consulta o PSG para diagnóstico sem consumir amostras nem alterar seus contadores.
        const Core::SN76489 &psg() const { return psg_; }

        // Transfere as amostras mono de 48 kHz geradas desde a última coleta, preservando a fase.
        void takeAudio(std::vector<float> &samples) override { psg_.takeSamples(samples); }

        // Encaminha os ganhos de apresentação ao mixer sem alterar o hardware programado pela ROM.
        void setAudioSettings(const Common::AudioSettings &settings) override { psg_.setAudioSettings(settings); }

        void setSpriteUnlimited(bool enabled) override { vdp_.setSpriteUnlimited(enabled); }

    private:
        // Aplica direções/botões 1/2; o turbo usa a publicação de quadros do VDP, inclusive em HALT/WAIT.
        void applyButtons();
        Core::TMS9918 vdp_;
        Core::SN76489 psg_;
        Core::IO::Joypad joypad_;
        Common::InputState buttons_{};
        std::uint64_t inputFrame_ = 0;
    };
}