#include "Common/Hash.h"
#include "Core/Systems/GameGear/GameGear.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace R2SEGA8;
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
                                     ("r2sega8-gg-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gg");
        Fixture()
        {
            std::ofstream file(path, std::ios::binary);
            file.put('\x76');
            for (int i = 1; i < 0x10000; ++i)
                file.put(static_cast<char>(i / 0x4000));
            file.close();
            check(bool(file), "ROM fixture failed");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };
    void vdpCommand(Core::GameGear &board, unsigned address, unsigned code)
    {
        board.output(0xbf, static_cast<std::uint8_t>(address));
        board.output(0xbf, static_cast<std::uint8_t>((code << 6) | ((address >> 8) & 0x3f)));
    }
    template <typename Action>
    void rejects(Action action, const char *message)
    {
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            return;
        }
        throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        Fixture fixture;
        auto original = std::make_unique<Core::GameGear>(Core::Cartridge(fixture.path));
        auto restored = std::make_unique<Core::GameGear>(Core::Cartridge(fixture.path));
        check(original->supportsSaveState(), "Game Gear must support save states");
        original->writeMemory(0xfffd, 3);
        original->writeMemory(0xfffc, 0x0c);
        original->writeMemory(0x8123, 0xa5);
        original->writeMemory(0xc100, 0x77);
        original->setInput({Common::mask(Common::Button::Start) | Common::mask(Common::Button::A), 0});
        original->output(0x7f, 0x90);
        original->output(0x06, 0x10);
        vdpCommand(*original, 0x20, 3);
        original->output(0xbe, 0x21);
        original->output(0xbe, 0x03);
        original->runCycles(61321);
        // Salva uma cor pendente entre a escrita par e a ímpar da CRAM.
        vdpCommand(*original, 0x20, 3);
        original->output(0xbe, 0xfe);
        const auto saved = original->saveState();
        restored->loadState(saved);
        check(restored->saveState() == saved, "Game Gear state must round-trip completely");
        check(restored->readMemory(0x8123) == 0xa5 && restored->readMemory(0xc100) == 0x77 &&
                  restored->readMemory(0x0400) == 3 && restored->input(0) == 0x5f && !(restored->input(0xdc) & 0x10),
              "Mapper, memory and controls must be restored");
        std::vector<float> discarded, actual, expected;
        original->takeAudio(discarded);
        original->output(0xbe, 0x04);
        restored->output(0xbe, 0x04);
        original->runCycles(80000);
        restored->runCycles(80000);
        original->takeAudio(expected);
        restored->takeAudio(actual);
        check(actual == expected && !actual.empty(), "Stereo filter, routing and sample phase must continue identically");
        check(original->saveState() == restored->saveState(), "Restored CPU and VDP must continue deterministically");
        const auto frame = restored->videoFrame();
        check(frame.rgba[0] == 238 && frame.rgba[1] == 255 && frame.rgba[2] == 68,
              "Pending palette latch must be restored");
        auto bad = saved;
        bad[0] ^= 1;
        const auto before = restored->saveState();
        rejects([&]
                { restored->loadState(bad); }, "Corrupt checksum must be rejected");
        check(restored->saveState() == before, "Checksum failure must preserve the active machine");
        bad.assign(saved.begin(), saved.begin() + 10000);
        auto checksum = Common::fnv1a64(bad);
        for (unsigned i = 0; i < 8; ++i)
            bad.push_back(static_cast<std::uint8_t>(checksum >> (i * 8)));
        rejects([&]
                { restored->loadState(bad); }, "Truncated payload with a valid checksum must be rejected");
        bad = saved;
        bad[24] = 2; // hasCartRam: booleano do cabeçalho.
        rewriteChecksum(bad);
        const auto beforeInvalid = restored->saveState();
        rejects([&]
                { restored->loadState(bad); }, "Forged state boolean must be rejected");
        check(restored->saveState() == beforeInvalid, "Rejected forged state must preserve the machine");
        bad = saved;
        bad[53] = 3; // Z80 IM: apenas 0, 1 ou 2.
        rewriteChecksum(bad);
        rejects([&]
                { restored->loadState(bad); }, "Forged CPU mode must be rejected");
        check(restored->saveState() == beforeInvalid, "Rejected CPU state must preserve the machine");
        auto sms = std::make_unique<Core::MasterSystem>(Core::Cartridge(fixture.path));
        rejects([&]
                { restored->loadState(sms->saveState()); }, "Other systems' states must be rejected");
        auto empty = std::make_unique<Core::GameGear>(Core::Cartridge(fixture.path));
        restored->loadState(empty->saveState());
        restored->writeMemory(0xfffc, 8);
        restored->writeMemory(0x8000, 0x42);
        check(restored->readMemory(0x8000) == 0x42, "Cartridge RAM must allocate after restoring a state without it");
        {
            std::fstream changed(fixture.path, std::ios::binary | std::ios::in | std::ios::out);
            changed.put('\x00');
            changed.close();
            check(bool(changed), "Unable to modify ROM fixture");
        }
        auto otherRom = std::make_unique<Core::GameGear>(Core::Cartridge(fixture.path));
        rejects([&]
                { otherRom->loadState(saved); }, "A different ROM must not accept the state");
        std::cout << "Game Gear save states passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}