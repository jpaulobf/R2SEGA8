#include "MasterSystem.h"
#include "Common/Hash.h"
#include "Core/State/StateArchive.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Core
{
    std::vector<std::uint8_t> MasterSystem::saveState() const
    {
        std::vector<std::uint8_t> data;
        StateArchive ar(data);
        std::uint32_t magic = 0x31534d53, version = 1;
        bool pal = pal_, hasCartRam = !cartridge_.ram().empty();
        std::uint64_t hash = Common::fnv1a64(cartridge_.rom()), size = cartridge_.rom().size();
        auto cpuState = cpu().snapshot();
        auto video = std::make_unique<SegaVDP::Snapshot>();
        vdp_.snapshot(*video);
        auto audio = psg_.snapshot();
        auto ram = ram_, cartRam = cartridge_.ram();
        auto banks = mapper_.state().banks;
        auto mapper = mapper_.state().control;
        auto buttons = controls_.buttons();
        auto frame = controls_.frame();
        ar(magic, version, pal, hash, size, hasCartRam);
        cpuState.stateArchive(ar);
        video->stateArchive(ar);
        audio.stateArchive(ar);
        ar(ram, cartRam, banks, mapper, buttons, frame);
        auto checksum = Common::fnv1a64(data);
        ar(checksum);
        return data;
    }

    void MasterSystem::loadState(const std::vector<std::uint8_t> &data)
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
        bool pal = false, hasCartRam = false;
        std::uint64_t hash = 0, size = 0;
        ar(magic, version, pal, hash, size, hasCartRam);
        if (magic != 0x31534d53 || version != 1 || pal != pal_ ||
            hash != Common::fnv1a64(cartridge_.rom()) || size != cartridge_.rom().size())
            throw std::runtime_error("Save state is incompatible with this system, ROM or console profile.");
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
        video->stateArchive(ar);
        audio.stateArchive(ar);
        ar(ram, cartRam, banks, mapper, buttons, frame);
        ar.finish();
        if ((mapper & 0x08) && !hasCartRam)
            throw std::runtime_error("Invalid save state cartridge RAM.");
        cpu().restore(cpuState);
        vdp_.restore(*video);
        psg_.restore(audio);
        ram_ = std::move(ram);
        cartridge_.ram() = std::move(cartRam);
        mapper_.setState({banks, mapper});
        controls_.setInput(buttons, frame);
    }

    MasterSystem::MasterSystem(Core::Cartridge cartridge, bool pal)
        : Board(Common::SystemType::MasterSystem, std::move(cartridge), 8 * 1024), pal_(pal), mapper_(cartridge_), vdp_(pal),
          psg_(pal ? 3546893 : 3579545, SN76489::DefaultSampleRate, SN76489::Variant::Sega)
    {
        // A execução começa direto no cartucho, sem a BIOS. Alguns jogos fazem
        // CALL/PUSH antes de definir SP e dependem da pilha deixada pela BIOS.
        // Em FFFF, essas escritas atingiriam os registradores do mapeador Sega.
        auto initialCPU = cpu().state();
        initialCPU.sp = 0xdff0;
        cpu().setState(initialCPU);
        // Sem BIOS, escolhemos uma inicialização compatível para os registradores
        // legados: no VDP SMS1, R3 e R4 zerados mascaram os endereços dos tiles.
        // Jogos continuam livres para reprogramá-los e usar essas máscaras.
        vdp_.writeControl(0xff);
        vdp_.writeControl(0x83);
        vdp_.writeControl(0xff);
        vdp_.writeControl(0x84);
    }

    std::uint8_t MasterSystem::readMemory(std::uint16_t address) const
    {
        return mapper_.read(cartridge_, ram_, address);
    }

    void MasterSystem::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        mapper_.write(cartridge_, ram_, address, value);
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