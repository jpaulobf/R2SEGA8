#include "../Support/CartridgeFixture.h"
#include "Common/Hash.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    class CollisionTrace
    {
    public:
        void run(R2SEGA8::Core::MasterSystem &board, unsigned cycles)
        {
            using Pins = R2SEGA8::Core::Z80Pins;
            auto &cpu = board.cpu();
            const auto &vdp = board.vdp().state();
            while (cycles && stage_ != Stage::Done)
            {
                // Endereços da ROM identificada pelo hash no main, a partir de VDPTest.sym.
                if (stage_ == Stage::Waiting && cpu.instructionBoundary() && cpu.state().pc == 0x11d9)
                {
                    stage_ = Stage::Running;
                    printPosition(board);
                    std::printf(" enter RunTest46\n");
                }
                const auto register1 = vdp.registers[1];
                const auto status = vdp.status;
                const auto &pins = cpu.pins();
                const bool readingStatus = pins.signals == (Pins::IO | Pins::Read) && (pins.address & 0xff) == 0xbf;
                cpu.tick();
                --cycles;
                if (stage_ != Stage::Running)
                    continue;

                if ((register1 ^ vdp.registers[1]) & 0x40)
                {
                    const unsigned sat = (vdp.registers[5] & 0x7e) << 7;
                    printPosition(board);
                    std::printf(" R1=$%02X SAT Y=$%02X/$%02X/$%02X +$84=$%02X +$89=$%02X\n",
                                unsigned(vdp.registers[1]), unsigned(vdp.vram[sat]), unsigned(vdp.vram[sat + 1]),
                                unsigned(vdp.vram[sat + 2]), unsigned(vdp.vram[sat + 0x84]), unsigned(vdp.vram[sat + 0x89]));
                }
                if (!(status & 0x20) && (vdp.status & 0x20))
                {
                    printPosition(board);
                    std::printf(" COL set\n");
                }
                const auto pc = cpu.state().pc;
                // Posições imediatamente após os dois IN A,($BF) da rotina.
                if (readingStatus && pins.dataValid && cpu.instructionBoundary() && (pc == 0x120a || pc == 0x1218))
                {
                    printPosition(board);
                    std::printf(" read $BF=$%02X (%s)\n", unsigned(pins.data), pc == 0x120a ? "clear" : "result");
                    if (pc == 0x1218)
                        stage_ = Stage::Done;
                }
            }
            board.runCycles(cycles);
        }

        bool complete() const { return stage_ == Stage::Done; }

    private:
        enum class Stage
        {
            Waiting,
            Running,
            Done
        };
        Stage stage_ = Stage::Waiting;

        static void printPosition(const R2SEGA8::Core::MasterSystem &board)
        {
            const auto &vdp = board.vdp().state();
            std::printf("T=%llu line=%u dot=%u phase=%u VC=$%02X",
                        static_cast<unsigned long long>(board.cpu().state().cycles), unsigned(vdp.line),
                        unsigned(vdp.dot), unsigned(vdp.masterPhase), unsigned(board.vdp().verticalCounter()));
        }
    };
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("Usage: sms_vdp_diagnostic VDPTest.sms [test number | --trace-46]");
        const bool traceCollision = argc == 3 && std::string_view(argv[2]) == "--trace-46";
        unsigned selected = 0;
        if (traceCollision)
            selected = 46;
        else if (argc == 3)
        {
            std::size_t end = 0;
            const auto number = std::stoul(argv[2], &end);
            if (argv[2][end] != '\0' || number < 1 || number > 60 || number == 20 || (number >= 37 && number <= 40))
                throw std::runtime_error("Invalid SMS VDP test number");
            selected = static_cast<unsigned>(number);
        }
        if (!std::filesystem::exists(argv[1]))
        {
            std::cerr << "SMS VDP Test ROM missing: " << argv[1] << '\n';
            return 77;
        }
        auto cart = ::R2SEGA8::Tests::loadCartridge(argv[1]);
        if (cart.rom().size() != 32768 || R2SEGA8::Common::fnv1a64(cart.rom()) != 0x113905aa6c257fd7ULL)
            throw std::runtime_error("Unexpected ROM: use the documented pinned build of SMS VDP Test.");
        R2SEGA8::Core::MasterSystem board(std::move(cart));
        for (unsigned i = 0; i < 60; ++i)
            board.writeMemory(0xc900 + i, 0x7e);
        constexpr unsigned frameCycles = 59736;
        bool completed = false;
        CollisionTrace trace;
        // Repeated button pulses enter the first menu and advance each results page.
        for (unsigned frame = 0; frame < 1800; ++frame)
        {
            board.setButtons(0, frame % 60 >= 30 && frame % 60 < 40 ? R2SEGA8::Common::mask(R2SEGA8::Common::Button::A) : 0);
            if (traceCollision && !trace.complete())
                trace.run(board, frameCycles);
            else
                board.runCycles(frameCycles);
            const auto pc = board.cpu().state().pc;
            // Last result page is complete only when its wait loop has been reached.
            if (board.readMemory(0xc93b) != 0x7e && pc >= 0x2f82 && pc < 0x2f90)
            {
                completed = true;
                break;
            }
        }
        if (traceCollision && !trace.complete())
            throw std::runtime_error("Trace did not reach the final status read of test 46.");
        unsigned passed = 0, failed = 0, missing = 0;
        for (unsigned n = 1; n <= 60; ++n)
        {
            if (n == 20 || (n >= 37 && n <= 40))
                continue;
            if (selected && n != selected)
                continue;
            const auto value = board.readMemory(0xc900 + n - 1);
            const bool pass = n == 21 ? value == 2 : value == 0;
            const bool absent = value == 0x7e;
            std::cout << std::setw(2) << n << "  " << (absent ? "INCOMPLETE" : pass ? "PASS"
                                                                                    : "FAIL")
                      << "  0x" << std::hex << std::setw(2) << std::setfill('0') << unsigned(value)
                      << std::dec << std::setfill(' ') << '\n';
            absent ? ++missing : pass ? ++passed
                                      : ++failed;
        }
        std::cout << passed << " passed, " << failed << " failed, " << missing << " incomplete\n";
        if (!completed)
            std::cout << "Final results page was not reached within the cycle budget.\n";
        return missing || !completed ? 2 : failed ? 1
                                                  : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 2;
    }
}