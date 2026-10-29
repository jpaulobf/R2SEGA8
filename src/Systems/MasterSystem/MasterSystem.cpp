#include "MasterSystem.h"
#include <utility>

namespace R2SEGA8::Systems
{
    MasterSystem::MasterSystem(Core::Cartridge cartridge)
        : System(Common::SystemType::MasterSystem, std::move(cartridge), 8 * 1024) {}
}