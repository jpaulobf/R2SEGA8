#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace R2SEGA8::Common
{
    // FNV-1a de 64 bits usado na identidade da ROM e no checksum dos estados salvos.
    inline std::uint64_t fnv1a64(const std::uint8_t *bytes, std::size_t size)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    inline std::uint64_t fnv1a64(const std::vector<std::uint8_t> &bytes)
    {
        return fnv1a64(bytes.data(), bytes.size());
    }
}