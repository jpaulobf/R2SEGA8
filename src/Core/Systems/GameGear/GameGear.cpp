#include "GameGear.h"
#include "Common/Hash.h"
#include "Core/State/StateArchive.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Core
{
    std::vector<std::uint8_t> GameGear::saveState() const
    {
        std::vector<std::uint8_t> data;
        StateArchive ar(data);
        std::uint32_t magic = 0x31534747, version = 1;
        std::uint64_t hash = Common::fnv1a64(cartridge_.rom()), size = cartridge_.rom().size();
        bool hasCartRam = !cartridge_.ram().empty();
        auto cpuState = cpu().snapshot();
        auto video = std::make_unique<SegaVDP::Snapshot>();
        vdp_.snapshot(*video);
        auto audio = psg_.snapshot();
        auto ram = ram_, cartRam = cartridge_.ram();
        auto banks = mapper_.state().banks;
        auto mapper = mapper_.state().control;
        auto buttons = controls_.buttons();
        auto frame = controls_.frame();
        ar(magic, version, hash, size, hasCartRam);
        cpuState.stateArchive(ar);
        video->stateArchiveGameGear(ar);
        audio.stateArchiveGameGear(ar);
        ar(ram, cartRam, banks, mapper, buttons, frame);
        auto checksum = Common::fnv1a64(data);
        ar(checksum);
        return data;
    }

    void GameGear::loadState(const std::vector<std::uint8_t> &data)
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
        std::uint32_t magic = 0, version = 0;
        std::uint64_t hash = 0, size = 0;
        bool hasCartRam = false;
        ar(magic, version, hash, size, hasCartRam);
        if (magic != 0x31534747 || version != 1 || hash != Common::fnv1a64(cartridge_.rom()) ||
            size != cartridge_.rom().size())
            throw std::runtime_error("Save state is incompatible with this system or ROM.");
        auto cpuState = cpu().snapshot();
        auto video = std::make_unique<SegaVDP::Snapshot>();
        auto audio = psg_.snapshot();
        auto ram = ram_;
        std::vector<std::uint8_t> cartRam(hasCartRam ? 0x8000 : 0);
        std::array<std::uint8_t, 3> banks{};
        std::uint8_t mapper = 0;
        Common::InputState buttons{};
        std::uint64_t frame = 0;
        cpuState.stateArchive(ar);
        video->stateArchiveGameGear(ar);
        audio.stateArchiveGameGear(ar);
        ar(ram, cartRam, banks, mapper, buttons, frame);
        ar.finish();
        if ((mapper & 0x08) && !hasCartRam)
            throw std::runtime_error("Invalid save state cartridge RAM.");
        // Na carga pelo menu a Engine restaura uma placa temporária antes de publicá-la.
        cpu().restore(cpuState);
        vdp_.restore(*video);
        psg_.restore(audio);
        ram_ = std::move(ram);
        cartridge_.ram() = std::move(cartRam);
        mapper_.setState({banks, mapper});
        controls_.setInput(buttons, frame);
    }

    GameGear::GameGear(Core::Cartridge cartridge)
        : Board(Common::SystemType::GameGear, std::move(cartridge), 8 * 1024), mapper_(cartridge_) {}

    std::uint8_t GameGear::readMemory(std::uint16_t address) const
    {
        return mapper_.read(cartridge_, ram_, address);
    }

    void GameGear::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        mapper_.write(cartridge_, ram_, address, value);
    }

    void GameGear::setButtons(unsigned player, Common::ButtonMask buttons)
    {
        setInput(controls_.withPlayer(player, buttons));
    }

    void GameGear::setInput(const Common::InputState &buttons)
    {
        controls_.setInput(buttons, vdp_.state().completedFrames);
    }

    std::uint8_t GameGear::input(std::uint16_t port)
    {
        // Perfil internacional NTSC; Start é uma linha ativa em zero, sem NMI.
        if ((port & 0xff) == 0x00)
            return static_cast<std::uint8_t>(0x5f | ((controls_.buttons()[0] & Common::mask(Common::Button::Start)) ? 0 : 0x80));
        // O controle integrado ocupa as seis linhas inferiores; o conector externo não é emulado.
        if ((port & 0xc0) == 0xc0)
            return (port & 1) ? 0xff : static_cast<std::uint8_t>(controls_.joypad().readEven() | 0xc0);
        if ((port & 0xc1) == 0x40)
            return vdp_.verticalCounter();
        if ((port & 0xc0) != 0x80)
            return 0xff;
        const auto value = (port & 1) ? vdp_.readStatus() : vdp_.readData();
        cpu().setIRQ(vdp_.interrupt());
        return value;
    }

    void GameGear::output(std::uint16_t port, std::uint8_t value)
    {
        // A porta estéreo seleciona os canais do PSG enviados a cada lado.
        if ((port & 0xff) == 0x06)
        {
            psg_.setStereoRouting(value);
            return;
        }
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

    void GameGear::clock(const Z80Pins &)
    {
        if (!peripheralClockDue())
            return;
        // VDP e PSG avançam junto com a CPU, respeitando o divisor de overclock.
        vdp_.runMasterTicks(3);
        psg_.tick();
        controls_.updateFrame(vdp_.state().completedFrames);
        cpu().setIRQ(vdp_.interrupt());
    }
}