#pragma once
#include "Systems/System.h"

namespace R2SEGA8::Systems
{
    // Representa o SG-1000 com um perfil inicial de ROM simples e RAM espelhada.
    // RAM de cartucho e cartuchos com seleção de bancos ainda não são implementados.
    class SG1000 final : public System
    {
    public:
        // Recebe o cartucho, aloca 1 KiB de RAM e rejeita ROMs maiores que 48 KiB.
        explicit SG1000(Core::Cartridge cartridge);
        // Informa que o perfil de ROM simples possui um mapa de memória implementado.
        bool hasMemoryMap() const override { return true; }
        // Lê a ROM em 0000–BFFF ou a RAM espelhada em C000–FFFF.
        // Retorna provisoriamente FF quando não há um byte de ROM no endereço.
        std::uint8_t readMemory(std::uint16_t address) const override;
        // Escreve na RAM espelhada em C000–FFFF; ignora escritas na região de ROM.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;
    };
}