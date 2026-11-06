#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>

namespace R2SEGA8::Common
{
    // Limite de alocação para imagens brutas; não representa um limite do hardware.
    inline constexpr std::size_t MaxRomFileSize = 16 * 1024 * 1024;
    
    // Lê todos os bytes de uma ROM bruta. Lança uma exceção para arquivos inválidos,
    // vazios, ZIP, acima do limite de alocação ou com falhas de leitura.
    inline constexpr std::size_t MaxZipFileSize = 128u * 1024 * 1024;
    std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path, std::string *romName = nullptr);
    bool extractRomFromZip(const std::vector<std::uint8_t> &zip, std::vector<std::uint8_t> &rom, std::string &romName);
}