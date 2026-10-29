#include "SC3000.h"
#include <utility>

namespace R2SEGA8::Systems
{
    SC3000::SC3000(Core::Cartridge cartridge)
        : System(Common::SystemType::SC3000, std::move(cartridge), 1024) {}
}