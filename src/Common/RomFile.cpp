#include "RomFile.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <zlib.h>
#include <stdexcept>

namespace R2SEGA8::Common
{
    namespace
    {
        // Verifica se o intervalo [p, p + n) cabe no vetor, sem somar antes da validação.
        bool has(const std::vector<uint8_t> &v, size_t p, size_t n)
        {
            return p <= v.size() && n <= v.size() - p;
        }

        // Lê um inteiro de 16 bits em ordem little-endian; o intervalo deve ser válido.
        uint16_t u16(const std::vector<uint8_t> &v, size_t p)
        {
            return uint16_t(v[p]) | (uint16_t(v[p + 1]) << 8);
        }

        // Lê um inteiro de 32 bits em ordem little-endian; o intervalo deve ser válido.
        uint32_t u32(const std::vector<uint8_t> &v, size_t p)
        {
            return uint32_t(u16(v, p)) | (uint32_t(u16(v, p + 2)) << 16);
        }

        // Identifica extensões de ROM aceitas, sem diferenciar maiúsculas de minúsculas.
        bool isRom(const std::string &name)
        {
            const auto dot = name.find_last_of('.');
            if (dot == std::string::npos)
                return false;
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c)
                           { return char(std::tolower(c)); });
            return ext == ".sg" || ext == ".sc" || ext == ".sms" || ext == ".gg";
        }
    }

    std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path, std::string *romName)
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
        auto ext = path.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        const bool zipFile = ext == ".zip";
        if (size > static_cast<std::streamoff>(zipFile ? MaxZipFileSize : MaxRomFileSize))
            throw std::runtime_error("ROM or ZIP file exceeds the supported size limit.");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Unable to read the complete ROM file.");
        if (file.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("The ROM file changed during loading. Try again.");
        if (zipFile)
        {
            std::vector<std::uint8_t> rom;
            std::string name;
            if (!extractRomFromZip(bytes, rom, name))
                throw std::runtime_error("Invalid, unsupported or oversized ZIP archive.");
            if (romName)
                *romName = std::move(name);
            return rom;
        }
        if (bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K' &&
            ((bytes[2] == 3 && bytes[3] == 4) || (bytes[2] == 5 && bytes[3] == 6) ||
             (bytes[2] == 7 && bytes[3] == 8)))
            throw std::runtime_error("ZIP archive must have a .zip extension.");
        if (romName)
            *romName = path.filename().u8string();
        return bytes;
    }

    bool extractRomFromZip(const std::vector<uint8_t> &zip, std::vector<uint8_t> &rom, std::string &romName)
    {
        // O diretório central fornece os metadados, inclusive quando há descritor de dados.
        if (zip.size() < 22 || zip.size() > MaxZipFileSize)
            return false;

        // Procura o fim do diretório central dentro do tamanho máximo do comentário ZIP.
        const size_t first = zip.size() > 65557 ? zip.size() - 65557 : 0;
        size_t end = zip.size() - 22;
        while (!(u32(zip, end) == 0x06054b50 && has(zip, end, 22) &&
                 u16(zip, end + 20) == zip.size() - end - 22))
        {
            if (end == first)
                return false;
            --end;
        }
        const uint16_t count = u16(zip, end + 10);
        const size_t cdSize = u32(zip, end + 12), cdOffset = u32(zip, end + 16);
        if (u16(zip, end + 4) || u16(zip, end + 6) || u16(zip, end + 8) != count ||
            count == 0xffff || cdOffset > end || cdSize != end - cdOffset)
            return false; // Arquivos divididos em discos e ZIP64 não são aceitos.

        size_t p = cdOffset;
        std::vector<uint8_t> selected;
        bool found = false;
        std::string selectedName;

        // Percorre todo o diretório, mesmo após encontrar a primeira ROM.
        for (unsigned i = 0; i < count; ++i)
        {
            if (!has(zip, p, 46) || p + 46 > end || u32(zip, p) != 0x02014b50)
                return false;
            const uint16_t flags = u16(zip, p + 8), method = u16(zip, p + 10);
            const uint32_t crc = u32(zip, p + 16), compressed = u32(zip, p + 20), unpacked = u32(zip, p + 24);
            const size_t nameLen = u16(zip, p + 28), extra = u16(zip, p + 30), comment = u16(zip, p + 32);
            const size_t local = u32(zip, p + 42), length = 46 + nameLen + extra + comment;
            if (length > end - p || u16(zip, p + 34) != 0)
                return false;
            const std::string name(reinterpret_cast<const char *>(zip.data() + p + 46), nameLen);
            if (!found && isRom(name))
            {
                // Aceita dados armazenados ou DEFLATE bruto e rejeita flags não suportadas.
                if ((flags & ~uint16_t(0x080e)) || (method != 0 && method != 8) ||
                    (method == 0 && (flags & 6)) || !unpacked || unpacked > MaxRomFileSize ||
                    compressed > MaxZipFileSize || !has(zip, local, 30) || local >= cdOffset ||
                    u32(zip, local) != 0x04034b50 || u16(zip, local + 6) != flags ||
                    u16(zip, local + 8) != method)
                    return false;
                // Confere o cabeçalho local e o nome com os dados do diretório central.
                const size_t localName = u16(zip, local + 26), localExtra = u16(zip, local + 28);
                if (localName != nameLen || local + 30 > cdOffset ||
                    localName + localExtra > cdOffset - local - 30 ||
                    std::memcmp(zip.data() + local + 30, name.data(), nameLen) != 0)
                    return false;
                const size_t data = local + 30 + localName + localExtra;
                if (compressed > cdOffset - data)
                    return false;
                if (!(flags & 8) && (u32(zip, local + 14) != crc || u32(zip, local + 18) != compressed ||
                                     u32(zip, local + 22) != unpacked))
                    return false;
                std::vector<uint8_t> result(unpacked);
                if (method == 0)
                {
                    if (compressed != unpacked)
                        return false;
                    std::memcpy(result.data(), zip.data() + data, unpacked);
                }
                else
                {
                    // O ZIP contém um fluxo DEFLATE bruto, sem cabeçalho zlib.
                    z_stream stream{};
                    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                        return false;
                    stream.next_in = const_cast<Bytef *>(zip.data() + data);
                    stream.avail_in = compressed;
                    stream.next_out = result.data();
                    stream.avail_out = unpacked;
                    const int status = inflate(&stream, Z_FINISH);
                    const bool ok = status == Z_STREAM_END && stream.total_in == compressed && stream.total_out == unpacked;
                    inflateEnd(&stream);
                    if (!ok)
                        return false;
                }
                // Confirma a integridade dos dados descompactados.
                if (crc32(0, result.data(), unpacked) != crc)
                    return false;
                selected = std::move(result);
                selectedName = name;
                found = true;
            }
            p += length;
        }
        if (!found || p != end)
            return false;
        rom = std::move(selected);
        romName = std::move(selectedName);
        return true;
    }
}