#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace R2SEGA8::Systems
{
    // Cria um arquivo novo e exclusivo no mesmo diretório, depois substitui o destino.
    void writeAtomicFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &data);
}