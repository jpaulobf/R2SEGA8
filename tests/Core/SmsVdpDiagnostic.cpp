#include "../Support/CartridgeFixture.h"
#include "Common/Hash.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("Usage: sms_vdp_diagnostic VDPTest.sms [test number]");
        unsigned selected = 0;
        if (argc == 3)
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
        // Repeated button pulses enter the first menu and advance each results page.
        for (unsigned frame = 0; frame < 1800; ++frame)
        {
            board.setButtons(0, frame % 60 >= 30 && frame % 60 < 40 ? R2SEGA8::Common::mask(R2SEGA8::Common::Button::A) : 0);
            board.runCycles(frameCycles);
            const auto pc = board.cpu().state().pc;
            // Last result page is complete only when its wait loop has been reached.
            if (board.readMemory(0xc93b) != 0x7e && pc >= 0x2f82 && pc < 0x2f90)
            {
                completed = true;
                break;
            }
        }
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