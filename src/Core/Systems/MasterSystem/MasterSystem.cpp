#include "MasterSystem.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Core
{
    MasterSystem::MasterSystem(Core::Cartridge cartridge, bool pal)
        : Board(Common::SystemType::MasterSystem, std::move(cartridge), 8 * 1024), pal_(pal), vdp_(pal),
          psg_(pal ? 3546893 : 3579545, SN76489::DefaultSampleRate, SN76489::Variant::Sega)
    {
        if (cartridge_.profile() != CartridgeProfile::PlainROM || cartridge_.rom().size() > 0x100000)
            throw std::runtime_error("The Sega mapper profile supports ROMs up to 1 MiB and does not support this cartridge profile.");
        // Linhas de endereço ausentes espelham bancos em imagens com tamanho potência de dois.
        std::size_t bankCount = 1;
        while (bankCount * 0x4000 < cartridge_.rom().size())
            bankCount *= 2;
        bankMask_ = bankCount - 1;
        // A execução começa direto no cartucho, sem a BIOS. Alguns jogos fazem
        // CALL/PUSH antes de definir SP e dependem da pilha deixada pela BIOS.
        // Em FFFF, essas escritas atingiriam os registradores do mapeador Sega.
        auto initialCPU = cpu().state();
        initialCPU.sp = 0xdff0;
        cpu().setState(initialCPU);
    }

    std::uint8_t MasterSystem::readMemory(std::uint16_t address) const
    {
        if (address >= 0xc000)
            return ram_[address & 0x1fff];
        if (address >= 0x8000 && (mapperControl_ & 0x08))
            return cartridge_.ram()[(mapperControl_ & 0x04 ? 0x4000 : 0) + (address & 0x3fff)];
        const auto slot = address >> 14;
        const auto bank = address < 0x0400 ? 0 : (banks_[slot] & bankMask_);
        const auto offset = bank * 0x4000 + (address & 0x3fff);
        return offset < cartridge_.rom().size() ? cartridge_.rom()[offset] : 0xff;
    }

    void MasterSystem::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xc000)
        {
            ram_[address & 0x1fff] = value;
            if (address == 0xfffc)
            {
                mapperControl_ = value;
                // Até haver identificação de RAM por cartucho, disponibiliza-se 32 KiB sob demanda.
                if ((value & 0x08) && cartridge_.ram().empty())
                    cartridge_.ram().resize(0x8000);
            }
            else if (address >= 0xfffd)
                banks_[address - 0xfffd] = value;
        }
        else if (address >= 0x8000 && (mapperControl_ & 0x08))
            cartridge_.ram()[(mapperControl_ & 0x04 ? 0x4000 : 0) + (address & 0x3fff)] = value;
    }

    std::uint8_t MasterSystem::input(std::uint16_t port)
    {
        if ((port & 0xc1) == 0x40)
            return vdp_.verticalCounter();
        if ((port & 0xc0) == 0xc0)
            return (port & 1) ? controls_.joypad().readOdd() : controls_.joypad().readEven();
        if ((port & 0xc0) != 0x80)
            return 0xff;
        const auto value = (port & 1) ? vdp_.readStatus() : vdp_.readData();
        cpu().setIRQ(vdp_.interrupt());
        return value;
    }

    void MasterSystem::setButtons(unsigned player, Common::ButtonMask buttons)
    {
        setInput(controls_.withPlayer(player, buttons));
    }

    void MasterSystem::setInput(const Common::InputState &buttons)
    {
        controls_.setInput(buttons, vdp_.state().completedFrames);
        // Start representa o botão Pause no console; a mesma linha combina as duas fontes.
        cpu().setNMI(((buttons[0] | buttons[1]) & Common::mask(Common::Button::Start)) != 0);
    }

    void MasterSystem::output(std::uint16_t port, std::uint8_t value)
    {
        if ((port & 0xc0) == 0x40)
        {
            psg_.write(value);
            return;
        }
        if ((port & 0xc0) != 0x80)
            return;
        if (port & 1)
            vdp_.writeControl(value);
        else
            vdp_.writeData(value);
        cpu().setIRQ(vdp_.interrupt());
    }

    void MasterSystem::clock(const Z80Pins &)
    {
        if (!peripheralClockDue())
            return;
        vdp_.runMasterTicks(3);
        psg_.tick();
        controls_.updateFrame(vdp_.state().completedFrames);
        cpu().setIRQ(vdp_.interrupt());
    }
}