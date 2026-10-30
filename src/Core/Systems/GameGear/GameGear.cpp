#include "GameGear.h"
#include <utility>

namespace R2SEGA8::Core
{
    GameGear::GameGear(Core::Cartridge cartridge)
        : Board(Common::SystemType::GameGear, std::move(cartridge), 8 * 1024) {}
}