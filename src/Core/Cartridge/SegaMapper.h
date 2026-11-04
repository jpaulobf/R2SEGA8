#pragma once

#include "Cartridge.h"
#include <array>
#include <cstdint>
#include <vector>

namespace R2SEGA8::Core
{
    // Mapeador Sega compartilhado por Master System e Game Gear.
    class SegaMapper
    {
    public:
        struct State
        {
            std::array<std::uint8_t, 3> banks{{0, 1, 2}};
            std::uint8_t control = 0;
        };

        // Inicializa os bancos e valida o perfil e o tamanho da ROM.
        explicit SegaMapper(const Cartridge &cartridge);
        // Lê a ROM bancária, a RAM do cartucho ou a RAM interna espelhada.
        std::uint8_t read(const Cartridge &cartridge, const std::vector<std::uint8_t> &ram, std::uint16_t address) const;
        // Escreve na RAM e atualiza os registradores do mapeador quando endereçados.
        void write(Cartridge &cartridge, std::vector<std::uint8_t> &ram, std::uint16_t address, std::uint8_t value);
        // Consulta os registradores de bancos e controle sem alterá-los.
        const State &state() const { return state_; }
        // Restaura os registradores do mapeador após a validação pela placa.
        void setState(const State &state) { state_ = state; }

    private:
        State state_{};
        std::size_t bankMask_ = 0;
    };
}