#include "Cartridge.h"
#include "Common/RomFile.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace R2SEGA8::Core
{
    namespace
    {
        // CRC-32/ISO-HDLC usado pelo catálogo de ROMs; não depende do caminho ou nome do jogo.
        std::uint32_t crc32(const std::vector<std::uint8_t> &bytes)
        {
            std::uint32_t crc = 0xffffffffu;
            for (const auto byte : bytes)
            {
                crc ^= byte;
                for (unsigned bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
            }
            return ~crc;
        }
    } // namespace

    Cartridge::Cartridge(const std::filesystem::path &path, CartridgeProfile profile)
        : path_(std::filesystem::absolute(path).lexically_normal()), rom_(Common::readRomFile(path_))
    {
        using Common::SystemType;
        suggested_ = Common::systemFromExtension(path_.extension().u8string());

        // MAME hash/sg1000.xml: The Castle (Japan), MPR-10159, 32 KiB + 8 KiB de RAM.
        // Somente essa imagem catalogada recebe o perfil automaticamente; ROMs desconhecidas continuam simples.
        profile_ = profile == CartridgeProfile::Automatic
                       ? (rom_.size() == 0x8000 && crc32(rom_) == 0x092f29d6u ? CartridgeProfile::CastleRAM : CartridgeProfile::PlainROM)
                       : profile;
        if (profile_ == CartridgeProfile::CastleRAM)
        {
            if (rom_.size() != 0x8000)
                throw std::runtime_error("The Castle cartridge profile requires a 32 KiB ROM.");
            ram_.resize(0x2000);
        }

        // Prioriza a posição convencional do cabeçalho para 32 KiB e depois as alternativas menores.
        // Os campos são sugestões, não provas de validade ou de identificação do mapeador.
        const std::string signature = "TMR SEGA";
        for (const std::size_t offset : {0x7ff0u, 0x3ff0u, 0x1ff0u})
        {
            if (rom_.size() < offset + 16 || !std::equal(signature.begin(), signature.end(), rom_.begin() + offset))
                continue;
            const auto region = static_cast<std::uint8_t>(rom_[offset + 15] >> 4);
            header_ = SegaHeader{offset, region, static_cast<std::uint8_t>(rom_[offset + 15] & 0x0f)};
            SystemType headerSystem = SystemType::Unknown;
            if (region == 3 || region == 4)
                headerSystem = SystemType::MasterSystem;
            else if (region >= 5 && region <= 7)
                headerSystem = SystemType::GameGear;
            conflicting_ = headerSystem != SystemType::Unknown && suggested_ != SystemType::Unknown && headerSystem != suggested_;
            if (headerSystem != SystemType::Unknown)
                suggested_ = headerSystem;
            break;
        }
    }
}