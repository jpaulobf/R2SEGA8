#include "SG1000.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Systems
{
    SG1000::SG1000(Core::Cartridge cartridge)
        : System(Common::SystemType::SG1000, std::move(cartridge), 1024)
    {
        if (cartridge_.rom().size() > 0xc000)
            throw std::runtime_error("The initial SG-1000 plain ROM profile supports up to 48 KiB. Banked cartridges are pending.");
    }

    std::uint8_t SG1000::readMemory(std::uint16_t address) const
    {
        if (address >= 0xc000)
            return ram_[address & 0x03ff];
        // Áreas sem ROM retornam provisoriamente 0xff, sem simulação do comportamento do barramento.
        return address < cartridge_.rom().size() ? cartridge_.rom()[address] : 0xff;
    }

    void SG1000::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xc000)
            ram_[address & 0x03ff] = value;
    }
}