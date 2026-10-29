#include "System.h"
#include "SG1000/SG1000.h"
#include "SC3000/SC3000.h"
#include "MasterSystem/MasterSystem.h"
#include "GameGear/GameGear.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Systems
{
    System::System(Common::SystemType type, Core::Cartridge cartridge, std::size_t ramSize)
        : cartridge_(std::move(cartridge)), ram_(ramSize, 0), type_(type) {}

    std::uint8_t System::readMemory(std::uint16_t) const
    {
        throw std::logic_error("Memory mapping is not implemented for this system yet.");
    }

    void System::writeMemory(std::uint16_t, std::uint8_t)
    {
        throw std::logic_error("Memory mapping is not implemented for this system yet.");
    }

    std::unique_ptr<System> createSystem(Common::SystemType type, Core::Cartridge cartridge)
    {
        using Common::SystemType;
        switch (type)
        {
        case SystemType::SG1000:
            return std::make_unique<SG1000>(std::move(cartridge));
        case SystemType::SC3000:
            return std::make_unique<SC3000>(std::move(cartridge));
        case SystemType::MasterSystem:
            return std::make_unique<MasterSystem>(std::move(cartridge));
        case SystemType::GameGear:
            return std::make_unique<GameGear>(std::move(cartridge));
        default:
            throw std::invalid_argument("Choose a system before loading the ROM.");
        }
    }
}