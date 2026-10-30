#pragma once
#include "Core/IO/Joypad.h"
#include "Core/VDP/TMS9918/TMS9918.h"
#include "Systems/Board.h"

namespace R2SEGA8::Systems
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

        // Perfil inicial de clock NTSC; o perfil PAL acompanhará a implementação do vídeo.
        unsigned clockFrequency() const override { return 3579545; }

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

        // Encaminha escritas dos espelhos 80–BF ao VDP; PSG e expansão continuam pendentes.
        void output(std::uint16_t port, std::uint8_t value) override;

        // Avança três clocks do cristal do VDP por T-state da CPU e atualiza INT.
        void clock(const Core::Z80Pins &pins) override;

        // Retorna o último quadro completo, sem expor o VDP à camada de apresentação.
        Core::VideoFrame videoFrame() const override { return vdp_.frame(); }

        // Consulta o VDP para depuração e validação, sem alterar latches ou sinais.
        const Core::TMS9918 &vdp() const { return vdp_; }

    private:
        // Aplica direções/botões 1/2; o turbo usa a publicação de quadros do VDP, inclusive em HALT/WAIT.
        void applyButtons();
        Core::TMS9918 vdp_;
        Core::IO::Joypad joypad_;
        Common::InputState buttons_{};
        std::uint64_t inputFrame_ = 0;
    };
}