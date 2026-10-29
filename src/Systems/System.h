#pragma once

#include "Core/Cartridge/Cartridge.h"
#include <memory>
#include <vector>

namespace R2SEGA8::Systems
{
    // Reúne o cartucho e a RAM de uma máquina e define o contrato de acesso à memória.
    // Cada sistema concreto determina seu mapeamento e suas particularidades.
    class System
    {
    public:
        // Permite destruir a máquina concreta por um ponteiro para a classe base.
        virtual ~System() = default;

        // Retorna o tipo da máquina instanciada.
        Common::SystemType type() const { return type_; }

        // Expõe o cartucho da máquina para consulta, sem transferir sua posse.
        const Core::Cartridge &cartridge() const { return cartridge_; }

        // Expõe os bytes físicos da RAM interna, sem aplicar espelhamento de endereços.
        const std::vector<std::uint8_t> &ram() const { return ram_; }

        // Indica se o perfil atual implementa acessos ao espaço de memória da CPU.
        virtual bool hasMemoryMap() const { return false; }

        // Lê um byte pelo endereço da CPU. A implementação base lança uma exceção
        // porque ainda não possui um mapa de memória.
        virtual std::uint8_t readMemory(std::uint16_t address) const;

        // Escreve um byte pelo endereço da CPU. A implementação base lança uma
        // exceção porque ainda não possui um mapa de memória.
        virtual void writeMemory(std::uint16_t address, std::uint8_t value);

    protected:
        // Recebe o cartucho e aloca ramSize bytes de RAM interna, inicialmente zerados.
        System(Common::SystemType type, Core::Cartridge cartridge, std::size_t ramSize);
        Core::Cartridge cartridge_;
        std::vector<std::uint8_t> ram_;

    private:
        Common::SystemType type_;
    };

    // Constrói a máquina do tipo solicitado e transfere o cartucho para ela.
    // Lança uma exceção para tipos desconhecidos ou perfis não suportados.
    // O chamador só deve substituir a máquina ativa após a construção terminar.
    std::unique_ptr<System> createSystem(Common::SystemType type, Core::Cartridge cartridge);
}