#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace R2SEGA8::Common
{
    // Limite de alocação para imagens brutas; não representa um limite do hardware.
    inline constexpr std::size_t MaxRomFileSize = 16 * 1024 * 1024;
    // Lê todos os bytes de uma ROM bruta. Lança uma exceção para arquivos inválidos,
    // vazios, ZIP, acima do limite de alocação ou com falhas de leitura.
    std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path);
}