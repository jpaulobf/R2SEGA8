#include "SG1000.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Systems
{
    SG1000::SG1000(Core::Cartridge cartridge) : Board(Common::SystemType::SG1000, std::move(cartridge), 1024)
    {
        if (cartridge_.rom().size() > 0xc000)
            throw std::runtime_error("The initial SG-1000 plain ROM profile supports up to 48 KiB. Banked cartridges are pending.");
    }

    std::uint8_t SG1000::readMemory(std::uint16_t address) const
    {
        if (address >= 0xc000)
            return ram_[address & 0x03ff];
        if (cartridge_.profile() == Core::CartridgeProfile::CastleRAM)
        {
            // O cartucho decodifica RAM somente em 8000–9FFF; A000–BFFF volta à ROM espelhada.
            if (address >= 0x8000 && address < 0xa000)
                return cartridge_.ram()[address & 0x1fff];
            return cartridge_.rom()[address & 0x7fff];
        }
        // Áreas sem ROM retornam provisoriamente 0xff, sem simulação do comportamento do barramento.
        return address < cartridge_.rom().size() ? cartridge_.rom()[address] : 0xff;
    }

    void SG1000::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xc000)
            ram_[address & 0x03ff] = value;
        else if (cartridge_.profile() == Core::CartridgeProfile::CastleRAM && address >= 0x8000 && address < 0xa000)
            cartridge_.ram()[address & 0x1fff] = value;
    }

    void SG1000::setButtons(unsigned player, Common::ButtonMask buttons)
    {
        if (player >= buttons_.size())
            throw std::out_of_range("Invalid controller player.");
        auto next = buttons_;
        next[player] = buttons;
        setInput(next);
    }

    void SG1000::setInput(const Common::InputState &buttons)
    {
        buttons_ = buttons;
        applyButtons();
        // Pause/Hold é um botão da máquina: qualquer Start mapeado mantém a mesma linha NMI.
        cpu().setNMI(((buttons_[0] | buttons_[1]) & Common::mask(Common::Button::Start)) != 0);
    }

    void SG1000::applyButtons()
    {
        inputFrame_ = vdp_.frame().number;
        for (unsigned player = 0; player < buttons_.size(); ++player)
            joypad_.setButtons(player, Common::controllerButtons(buttons_[player], inputFrame_));
    }

    std::uint8_t SG1000::input(std::uint16_t port)
    {
        if ((port & 0xc0) == 0xc0)
            return (port & 1) ? joypad_.readOdd() : joypad_.readEven();
        if ((port & 0xc0) != 0x80)
            return 0xff;
        const auto value = (port & 1) ? vdp_.readStatus() : vdp_.readData();
        cpu().setIRQ(vdp_.interrupt());
        return value;
    }

    void SG1000::output(std::uint16_t port, std::uint8_t value)
    {
        if ((port & 0xc0) == 0x40)
        {
            psg_.write(value);
            cpu().setWAIT(!psg_.ready());
            return;
        }
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
        // PSG e CPU recebem o mesmo clock; WAIT não interrompe os periféricos.
        const bool wasReady = psg_.ready();
        psg_.tick();
        if (!psg_.ready())
            cpu().setWAIT(true);
        else if (!wasReady)
            cpu().setWAIT(false);
        if (vdp_.frame().number != inputFrame_)
            applyButtons();
        cpu().setIRQ(vdp_.interrupt());
    }
}