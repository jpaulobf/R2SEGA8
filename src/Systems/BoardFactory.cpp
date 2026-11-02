#include "BoardFactory.h"
#include "Core/Systems/SG1000/SG1000.h"
#include "Core/Systems/SC3000/SC3000.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include "Core/Systems/GameGear/GameGear.h"
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <utility>

namespace R2SEGA8::Systems
{
    std::unique_ptr<Core::Board> createBoard(Common::SystemType type, Core::Cartridge cartridge, Common::ConsoleProfile profile)
    {
        using Common::SystemType;
        switch (type)
        {
        case SystemType::SG1000:
            return std::make_unique<Core::SG1000>(std::move(cartridge));
        case SystemType::SC3000:
            return std::make_unique<Core::SC3000>(std::move(cartridge));
        case SystemType::MasterSystem:
        {
            auto name = cartridge.path().filename().u8string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            const bool european = name.find("(europe") != std::string::npos || name.find("(australia") != std::string::npos;
            const bool pal = profile == Common::ConsoleProfile::PAL || (profile == Common::ConsoleProfile::Auto && european);
            return std::make_unique<Core::MasterSystem>(std::move(cartridge), pal);
        }
        case SystemType::GameGear:
            return std::make_unique<Core::GameGear>(std::move(cartridge));
        default:
            throw std::invalid_argument("Choose a system before loading the ROM.");
        }
    }
}