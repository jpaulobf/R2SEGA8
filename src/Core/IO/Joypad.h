#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace R2SEGA8::Core::IO
{
    // Dois controles Sega de seis linhas; o hardware entrega pressionado como bit zero.
    class Joypad
    {
    public:
        // Libera todas as linhas dos dois jogadores.
        void reset() { buttons_.fill(0); }

        // Recebe direções e botões 1/2 ativos em 1; rejeita índices fora dos dois conectores.
        void setButtons(unsigned player, std::uint8_t buttons)
        {
            if (player >= buttons_.size())
                throw std::out_of_range("Invalid controller player.");
            buttons_[player] = buttons & 0x3f;
        }

        // Lê DC/DE: seis linhas de P1 e as direções Up/Down de P2 nos dois bits altos.
        std::uint8_t readEven() const { return static_cast<std::uint8_t>(~(buttons_[0] | ((buttons_[1] & 3) << 6))); }

        // Lê DD/DF: Left/Right/1/2 de P2; linhas de expansão ausentes permanecem altas.
        std::uint8_t readOdd() const { return static_cast<std::uint8_t>(0xf0 | ((static_cast<std::uint8_t>(~buttons_[1]) >> 2) & 15)); }

    private:
        std::array<std::uint8_t, 2> buttons_{};
    };
}