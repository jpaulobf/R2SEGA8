#include "SG1000.h"
#include "Common/Hash.h"
#include "Core/State/StateArchive.h"
#include <stdexcept>
#include <memory>
#include <utility>

namespace R2SEGA8::Core
{
    std::vector<std::uint8_t> SG1000::saveState() const
    {
        std::vector<std::uint8_t> data;
        StateArchive ar(data);
        std::uint32_t magic = 0x30314753, version = 1, profile = static_cast<std::uint32_t>(cartridge_.profile());
        std::uint64_t hash = Common::fnv1a64(cartridge_.rom()), size = cartridge_.rom().size();
        auto cpuState = cpu().snapshot();
        auto video = std::make_unique<TMS9918::State>(vdp_.state());
        auto audio = psg_.snapshot();
        auto ram = ram_, cartRam = cartridge_.ram();
        auto buttons = buttons_;
        auto frame = inputFrame_;
        ar(magic, version, profile, hash, size);
        cpuState.stateArchive(ar);
        video->stateArchive(ar);
        audio.stateArchive(ar);
        ar(ram, cartRam, buttons, frame);
        auto checksum = Common::fnv1a64(data);
        ar(checksum);
        return data;
    }

    void SG1000::loadState(const std::vector<std::uint8_t> &data)
    {
        if (data.size() < sizeof(std::uint64_t))
            throw std::runtime_error("Truncated save state.");
        const auto payloadSize = data.size() - sizeof(std::uint64_t);
        std::uint64_t checksum = 0;
        for (unsigned i = 0; i < 8; ++i)
            checksum |= static_cast<std::uint64_t>(data[payloadSize + i]) << (i * 8);
        if (checksum != Common::fnv1a64(data.data(), payloadSize))
            throw std::runtime_error("Save state checksum mismatch.");
        StateArchive ar(data, payloadSize);
        std::uint32_t magic = 0, version = 0, profile = 0;
        std::uint64_t hash = 0, size = 0;
        ar(magic, version, profile, hash, size);
        if (magic != 0x30314753 || version != 1 || profile != static_cast<std::uint32_t>(cartridge_.profile()) ||
            hash != Common::fnv1a64(cartridge_.rom()) || size != cartridge_.rom().size())
            throw std::runtime_error("Save state is incompatible with this system or ROM.");
        auto cpuState = cpu().snapshot();
        auto video = std::make_unique<TMS9918::State>(vdp_.state());
        auto audio = psg_.snapshot();
        auto ram = ram_, cartRam = cartridge_.ram();
        Common::InputState buttons{};
        std::uint64_t frame = 0;
        cpuState.stateArchive(ar);
        video->stateArchive(ar);
        audio.stateArchive(ar);
        ar(ram, cartRam, buttons, frame);
        ar.finish();
        // Validadores dos componentes operam antes de publicar a máquina temporária na aplicação.
        cpu().restore(cpuState);
        vdp_.restore(*video);
        psg_.restore(audio);
        ram_ = std::move(ram);
        cartridge_.ram() = std::move(cartRam);
        buttons_ = buttons;
        inputFrame_ = frame;
        for (unsigned player = 0; player < buttons_.size(); ++player)
            joypad_.setButtons(player, Common::controllerButtons(buttons_[player], inputFrame_));
    }
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
        inputFrame_ = vdp_.state().completedFrames;
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
        if (vdp_.state().completedFrames != inputFrame_)
            applyButtons();
        cpu().setIRQ(vdp_.interrupt());
    }
}