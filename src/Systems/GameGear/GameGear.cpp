#include "GameGear.h"
#include <utility>

namespace R2SEGA8::Systems
{
    GameGear::GameGear(Core::Cartridge cartridge)
        : Board(Common::SystemType::GameGear, std::move(cartridge), 8 * 1024) {}
}