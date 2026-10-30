#pragma once
#include "Systems/Board.h"
#include "Core/VDP/TMS9918/TMS9918.h"

namespace R2SEGA8::Systems
{
    // Representa o SG-1000 com um perfil inicial de ROM simples e RAM espelhada.
    // RAM de cartucho e cartuchos com seleção de bancos ainda não são implementados.
    class SG1000 final : public Board
    {
    public:
        // Recebe o cartucho, aloca 1 KiB de RAM e rejeita ROMs maiores que 48 KiB.
        explicit SG1000(Core::Cartridge cartridge);

        // Informa que o perfil de ROM simples possui um mapa de memória implementado.
        bool hasMemoryMap() const override { return true; }

        // Perfil inicial de clock NTSC; o perfil PAL acompanhará a implementação do vídeo.
        unsigned clockFrequency() const override { return 3579545; }

        // Lê a ROM em 0000–BFFF ou a RAM espelhada em C000–FFFF.
        // Retorna provisoriamente FF quando não há um byte de ROM no endereço.
        std::uint8_t readMemory(std::uint16_t address) const override;

        // Escreve na RAM espelhada em C000–FFFF; ignora escritas na região de ROM.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;

        // Encaminha leituras dos espelhos 80–BF ao VDP; portas restantes retornam FF provisoriamente.
        std::uint8_t input(std::uint16_t port) override;

        // Encaminha escritas dos espelhos 80–BF ao VDP; PSG e controles continuam pendentes.
        void output(std::uint16_t port, std::uint8_t value) override;

        // Avança três clocks do cristal do VDP por T-state da CPU e atualiza INT.
        void clock(const Core::Z80Pins &pins) override;

        // Retorna o último quadro completo, sem expor o VDP à camada de apresentação.
        Core::VideoFrame videoFrame() const override { return vdp_.frame(); }

        // Consulta o VDP para depuração e validação, sem alterar latches ou sinais.
        const Core::TMS9918 &vdp() const { return vdp_; }

    private:
        Core::TMS9918 vdp_;
    };
}