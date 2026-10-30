#include "MasterSystem.h"
#include <utility>

namespace R2SEGA8::Core
{
    MasterSystem::MasterSystem(Core::Cartridge cartridge)
        : Board(Common::SystemType::MasterSystem, std::move(cartridge), 8 * 1024) {}
}