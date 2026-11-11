#pragma once

#include "Common/RomFile.h"
#include "Core/Cartridge/Cartridge.h"
#include <utility>

namespace R2SEGA8::Tests
{
    inline Core::Cartridge loadCartridge(const std::filesystem::path &path,
                                         Core::CartridgeProfile profile = Core::CartridgeProfile::Automatic)
    {
        std::string name;
        auto bytes = Common::readRomFile(path, &name);
        return Core::Cartridge(path, std::move(bytes), std::move(name), profile);
    }
}