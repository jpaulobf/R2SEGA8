#include "Core/Systems/MasterSystem/MasterSystem.h"
#include "Common/Hash.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("r2sega8-sms-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sms");
        Fixture() { std::ofstream file(path, std::ios::binary); for (int i = 0; i < 0x10000; ++i) file.put(static_cast<char>(i / 0x4000)); check(bool(file), "ROM fixture failed"); }
        ~Fixture() { std::error_code ec; std::filesystem::remove(path, ec); }
    };
}

int main()
{
    try
    {
        using namespace R2SEGA8;
        Fixture fixture;
        for (bool pal : {false, true})
        {
            auto original = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), pal);
            auto restored = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), pal);
            check(original->supportsSaveState(), "Master System must support save states");
            original->writeMemory(0xfffd, 3);
            original->writeMemory(0xfffc, 0x0c);
            original->writeMemory(0x8123, 0xa5);
            original->writeMemory(0xc100, 0x77);
            original->output(0x7f, 0x91);
            original->runCycles(61321);
            const auto saved = original->saveState();
            restored->loadState(saved);
            check(restored->saveState() == saved, "State must round-trip with cartridge RAM and mapper");
            check(restored->readMemory(0x8123) == 0xa5 && restored->readMemory(0xc100) == 0x77 && restored->readMemory(0x400) == 3,
                  "Mapped ROM and RAM must be restored");
            original->runCycles(80000);
            restored->runCycles(80000);
            check(original->saveState() == restored->saveState(), "Restored machine must continue deterministically");
            auto bad = saved;
            bad[0] ^= 1;
            bool rejected = false;
            try { restored->loadState(bad); } catch (const std::exception &) { rejected = true; }
            check(rejected, "Corrupted state must be rejected");
            bad.assign(saved.begin(), saved.begin() + 10000);
            const auto checksum = Common::fnv1a64(bad);
            for (unsigned i = 0; i < 8; ++i) bad.push_back(static_cast<std::uint8_t>(checksum >> (8 * i)));
            rejected = false;
            try { restored->loadState(bad); } catch (const std::exception &) { rejected = true; }
            check(rejected, "Truncated payload with a valid checksum must be rejected");
            auto wrongProfile = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), !pal);
            rejected = false;
            try { wrongProfile->loadState(saved); } catch (const std::exception &) { rejected = true; }
            check(rejected, "Different console profile must be rejected");
            auto empty = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), pal);
            restored->loadState(empty->saveState());
            restored->writeMemory(0xfffc, 8);
            restored->writeMemory(0x8000, 0x42);
            check(restored->readMemory(0x8000) == 0x42, "Cartridge RAM must be allocated after loading a state without it");
        }
        std::cout << "Master System state tests passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
