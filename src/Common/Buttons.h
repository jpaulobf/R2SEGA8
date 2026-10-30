#pragma once
#include <array>
#include <cstdint>

namespace R2SEGA8::Common
{
    using ButtonMask = std::uint16_t;
    // Intenções lógicas ativas em 1; Select/Start são interpretados pela placa de cada console.
    enum class Button : ButtonMask
    {
        Up = 0x001,
        Down = 0x002,
        Left = 0x004,
        Right = 0x008,
        A = 0x010,
        B = 0x020,
        Select = 0x040,
        Start = 0x080,
        TurboA = 0x100,
        TurboB = 0x200
    };
    using InputState = std::array<ButtonMask, 2>;

    // Converte um botão lógico em máscara sem introduzir dependências de teclado ou SDL.
    constexpr ButtonMask mask(Button button)
    {
        return static_cast<ButtonMask>(button);
    }

    // Resolve o turbo em um de cada quatro quadros emulados; uma pressão normal sempre prevalece.
    constexpr std::uint8_t controllerButtons(ButtonMask buttons, std::uint64_t frame)
    {
        return static_cast<std::uint8_t>((buttons & 0x3f) | (frame % 4 == 3 ? ((buttons & mask(Button::TurboA) ? mask(Button::A) : 0) |
                                                                               (buttons & mask(Button::TurboB) ? mask(Button::B) : 0))
                                                                            : 0));
    }
}