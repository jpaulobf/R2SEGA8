#include "SegaMapper.h"
#include <stdexcept>

namespace R2SEGA8::Core
{
    SegaMapper::SegaMapper(const Cartridge &cartridge)
    {
        if (cartridge.profile() != CartridgeProfile::PlainROM || cartridge.rom().size() > 0x100000)
            throw std::runtime_error("The Sega mapper profile supports ROMs up to 1 MiB and does not support this cartridge profile.");
        // Linhas de endereço ausentes espelham bancos em potências de dois.
        std::size_t bankCount = 1;
        while (bankCount * 0x4000 < cartridge.rom().size())
            bankCount *= 2;
        bankMask_ = bankCount - 1;
    }

    std::uint8_t SegaMapper::read(const Cartridge &cartridge, const std::vector<std::uint8_t> &ram, std::uint16_t address) const
    {
        if (address >= 0xc000)
            return ram[address & 0x1fff];
        if (address >= 0x8000 && (state_.control & 0x08))
            return cartridge.ram()[(state_.control & 0x04 ? 0x4000 : 0) + (address & 0x3fff)];
        const auto slot = address >> 14;
        // O primeiro KiB permanece no banco zero mesmo após uma troca de banco.
        const auto bank = address < 0x0400 ? 0 : (state_.banks[slot] & bankMask_);
        const auto offset = bank * 0x4000 + (address & 0x3fff);
        return offset < cartridge.rom().size() ? cartridge.rom()[offset] : 0xff;
    }

    void SegaMapper::write(Cartridge &cartridge, std::vector<std::uint8_t> &ram, std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xc000)
        {
            ram[address & 0x1fff] = value;
            if (address == 0xfffc)
            {
                state_.control = value;
                // A RAM do cartucho é reservada sob demanda ao ser habilitada.
                if ((value & 0x08) && cartridge.ram().empty())
                    cartridge.ram().resize(0x8000);
            }
            else if (address >= 0xfffd)
                state_.banks[address - 0xfffd] = value;
        }
        else if (address >= 0x8000 && (state_.control & 0x08))
            cartridge.ram()[(state_.control & 0x04 ? 0x4000 : 0) + (address & 0x3fff)] = value;
    }
}