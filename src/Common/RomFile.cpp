#include "RomFile.h"
#include <fstream>
#include <stdexcept>

namespace R2SEGA8::Common
{
    std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            throw std::runtime_error("Select a readable regular ROM file.");
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Unable to open the ROM file.");
        const auto size = file.tellg();
        if (size <= 0)
            throw std::runtime_error("The ROM file is empty or its size cannot be read.");
        if (size > static_cast<std::streamoff>(MaxRomFileSize))
            throw std::runtime_error("ROM files larger than 16 MiB are not supported by this loader.");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Unable to read the complete ROM file.");
        if (file.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("The ROM file changed during loading. Try again.");
        // Arquivos compactados precisam ser extraídos antes de tratar seus bytes como uma ROM.
        if (bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K' &&
            ((bytes[2] == 3 && bytes[3] == 4) || (bytes[2] == 5 && bytes[3] == 6) ||
             (bytes[2] == 7 && bytes[3] == 8)))
            throw std::runtime_error("ZIP archives are not supported yet. Extract the ROM first.");
        return bytes;
    }
}