#pragma once
#include <cstddef>
#include <cstdint>

namespace R2SEGA8::Core
{
    // Consulta de um quadro completo, com bytes R, G, B, A nessa ordem e sem dependência gráfica.
    struct VideoFrame
    {
        const std::uint8_t *rgba = nullptr;
        unsigned width = 0, height = 0;
        std::size_t pitch = 0;
        std::uint64_t number = 0;
    };
}