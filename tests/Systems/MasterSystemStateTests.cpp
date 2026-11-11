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
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    void rewriteChecksum(std::vector<std::uint8_t> &state)
    {
        const auto payload = state.size() - 8;
        const auto checksum = R2SEGA8::Common::fnv1a64(state.data(), payload);
        for (unsigned i = 0; i < 8; ++i)
            state[payload + i] = static_cast<std::uint8_t>(checksum >> (8 * i));
    }
    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("r2sega8-sms-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sms");
        Fixture()
        {
            std::ofstream file(path, std::ios::binary);
            for (int i = 0; i < 0x10000; ++i)
                file.put(static_cast<char>(i / 0x4000));
            check(bool(file), "ROM fixture failed");
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
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
            // Version 1 predates the IO control and horizontal latch bytes.
            auto old = saved;
            old[4] = 1;
            old.erase(old.end() - 10, old.end());
            auto oldChecksum = Common::fnv1a64(old);
            for (unsigned i = 0; i < 8; ++i)
                old.push_back(static_cast<std::uint8_t>(oldChecksum >> (8 * i)));
            restored->loadState(old);
            check(restored->input(0x7f) == 0 && (restored->input(0xdd) & 0xc0) == 0xc0,
                  "Version 1 states must restore default IO and H counter latch");
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
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "Corrupted state must be rejected");
            bad.assign(saved.begin(), saved.begin() + 10000);
            const auto checksum = Common::fnv1a64(bad);
            for (unsigned i = 0; i < 8; ++i)
                bad.push_back(static_cast<std::uint8_t>(checksum >> (8 * i)));
            rejected = false;
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "Truncated payload with a valid checksum must be rejected");
            // O checksum pode ser recalculado por quem forneceu o arquivo.
            const auto beforeInvalid = restored->saveState();
            bad = saved;
            bad[25] = 2; // hasCartRam: booleano do cabeçalho.
            rewriteChecksum(bad);
            rejected = false;
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected && restored->saveState() == beforeInvalid, "Forged state boolean must be rejected atomically");
            bad = saved;
            bad[54] = 3; // Z80 IM: apenas 0, 1 ou 2.
            rewriteChecksum(bad);
            rejected = false;
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected && restored->saveState() == beforeInvalid, "Forged CPU mode must be rejected atomically");
            bad = saved;
            // Cabeçalho SMS: 26 bytes; dois estados Z80: 49 cada; pins: 9;
            // 272 ciclos de 18 bytes; length ocupa os últimos 4 bytes do ciclo ativo.
            constexpr std::size_t activeCycle = 26 + 2 * 49 + 9 + 272 * 18;
            for (std::size_t i = 10; i < 18; ++i)
                bad[activeCycle + i] = 0;  // phase e length = 0.
            bad[activeCycle + 18 + 4] = 1; // active = true.
            rewriteChecksum(bad);
            rejected = false;
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected && restored->saveState() == beforeInvalid, "Active CPU cycle with zero length must be rejected");
            bad = saved;
            bad[26] ^= 0x80; // Registrador A: alteração válida, mas não autêntica.
            rewriteChecksum(bad);
            auto altered = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), pal);
            altered->loadState(bad);
            altered->runCycles(2000);
            auto wrongProfile = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), !pal);
            rejected = false;
            try
            {
                wrongProfile->loadState(saved);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "Different console profile must be rejected");
            auto empty = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path), pal);
            restored->loadState(empty->saveState());
            restored->writeMemory(0xfffc, 8);
            restored->writeMemory(0x8000, 0x42);
            check(restored->readMemory(0x8000) == 0x42, "Cartridge RAM must be allocated after loading a state without it");
        }
        std::cout << "Master System state tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}