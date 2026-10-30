#include "SG1000.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Systems
{
    SG1000::SG1000(Core::Cartridge cartridge)
        : Board(Common::SystemType::SG1000, std::move(cartridge), 1024)
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

    std::uint8_t SG1000::input(std::uint16_t port)
    {
        if ((port & 0xc0) != 0x80)
            return 0xff;
        const auto value = (port & 1) ? vdp_.readStatus() : vdp_.readData();
        cpu().setIRQ(vdp_.interrupt());
        return value;
    }

    void SG1000::output(std::uint16_t port, std::uint8_t value)
    {
        if ((port & 0xc0) != 0x80)
            return;
        if (port & 1)
            vdp_.writeControl(value);
        else
            vdp_.writeData(value);
        // Habilitar INT com VBlank já pendente também altera a linha imediatamente.
        cpu().setIRQ(vdp_.interrupt());
    }

    void SG1000::clock(const Core::Z80Pins &)
    {
        // O cristal é dividido por três para a CPU e por dois para os pixels do VDP.
        vdp_.runMasterTicks(3);
        cpu().setIRQ(vdp_.interrupt());
    }
}