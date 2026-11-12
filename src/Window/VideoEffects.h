#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace R2SEGA8::Window
{
    // Copia o quadro e escurece as linhas alternadas sem alterar os bytes do núcleo.
    inline void applyScanlines(const std::uint8_t *source, unsigned sourcePitch, unsigned width,
                               unsigned height, unsigned crop, unsigned level, std::vector<std::uint8_t> &output)
    {
        const auto rowBytes = static_cast<std::size_t>(width) * 4;
        output.resize(rowBytes * height);
        level = std::clamp(level, 5u, 25u);
        for (unsigned y = 0; y < height; ++y)
        {
            const auto *row = source + static_cast<std::size_t>(y) * sourcePitch + crop * 4;
            auto *destination = output.data() + static_cast<std::size_t>(y) * rowBytes;
            std::copy_n(row, rowBytes, destination);
            if (y % 2 != 0)
                for (std::size_t x = 0; x < rowBytes; x += 4)
                    for (unsigned channel = 0; channel < 3; ++channel)
                        destination[x + channel] = static_cast<std::uint8_t>(destination[x + channel] * (100 - level) / 100);
        }
    }
}