#include "Cartridge.h"
#include "Common/RomFile.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace R2SEGA8::Core
{
    Cartridge::Cartridge(const std::filesystem::path &path)
        : path_(std::filesystem::absolute(path).lexically_normal()), rom_(Common::readRomFile(path_))
    {
        auto extension = path_.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        using Common::SystemType;
        if (extension == ".sg")
            suggested_ = SystemType::SG1000;
        else if (extension == ".sc")
            suggested_ = SystemType::SC3000;
        else if (extension == ".sms")
            suggested_ = SystemType::MasterSystem;
        else if (extension == ".gg")
            suggested_ = SystemType::GameGear;

        // Prioriza a posição convencional do cabeçalho para 32 KiB e depois as alternativas menores.
        // Os campos são sugestões, não provas de validade ou de identificação do mapeador.
        const std::string signature = "TMR SEGA";
        for (const std::size_t offset : {0x7ff0u, 0x3ff0u, 0x1ff0u})
        {
            if (rom_.size() < offset + 16 ||
                !std::equal(signature.begin(), signature.end(), rom_.begin() + offset))
                continue;
            const auto region = static_cast<std::uint8_t>(rom_[offset + 15] >> 4);
            header_ = SegaHeader{offset, region, static_cast<std::uint8_t>(rom_[offset + 15] & 0x0f)};
            SystemType headerSystem = SystemType::Unknown;
            if (region == 3 || region == 4)
                headerSystem = SystemType::MasterSystem;
            else if (region >= 5 && region <= 7)
                headerSystem = SystemType::GameGear;
            conflicting_ = headerSystem != SystemType::Unknown && suggested_ != SystemType::Unknown &&
                           headerSystem != suggested_;
            if (headerSystem != SystemType::Unknown)
                suggested_ = headerSystem;
            break;
        }
    }
}