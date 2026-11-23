#include "../Support/CartridgeFixture.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace R2SEGA8;
    using Common::Button;
    using Common::mask;

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        std::filesystem::path path;
        Fixture()
        {
            path = std::filesystem::temp_directory_path() /
                   ("r2sega8-sms-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sms");
            std::vector<std::uint8_t> rom(0x100, 0);
            const std::uint8_t code[] = {0xf3, 0x31, 0xf0, 0xdf, 0xdb, 0xdc, 0x32, 0, 0xc0, 0xdb, 0xdd,
                                         0x32, 1, 0xc0, 0x76, 0x18, 0xfd};
            const std::uint8_t nmi[] = {0xf5, 0x3a, 0x10, 0xc0, 0x3c, 0x32, 0x10, 0xc0, 0xf1, 0xed, 0x45};
            std::copy(std::begin(code), std::end(code), rom.begin());
            std::copy(std::begin(nmi), std::end(nmi), rom.begin() + 0x66);
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()), static_cast<std::streamsize>(rom.size()));
            file.close();
            check(bool(file), "Cannot write SMS input fixture");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    void controls(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        board->setInput({mask(Button::Up) | mask(Button::A), mask(Button::Down) | mask(Button::Right) | mask(Button::B)});
        for (unsigned port = 0xc0; port <= 0xff; ++port)
            check(board->input(static_cast<std::uint16_t>(0xa500 | port)) == (port & 1 ? 0xf5 : 0x6e), "SMS controller mirror");
        board->runCycles(256);
        check(board->readMemory(0xc000) == 0x6e && board->readMemory(0xc001) == 0xf5 && board->cpu().state().halted,
              "Z80 must read both controller ports");
        board->setInput({mask(Button::Start), 0});
        check(board->input(0xdc) == 0xff && board->input(0xdd) == 0xff, "Console Pause is not a controller button");
        board->runCycles(256);
        check(board->readMemory(0xc010) == 1, "Pause must trigger the NMI handler");
        board->setInput({0, mask(Button::Start)});
        board->runCycles(256);
        check(board->readMemory(0xc010) == 1, "Changing held Pause source must not trigger another NMI");
        board->setInput({});
        board->setInput({0, mask(Button::Start)});
        board->runCycles(256);
        check(board->readMemory(0xc010) == 2, "Pause triggers again after release");
        board->setInput({mask(Button::Select), 0});
        check(board->input(0xdc) == 0xff && board->input(0xdd) == 0xff, "Select has no basic SMS controller line");
        bool rejected = false;
        try
        {
            board->setButtons(2, 0);
        }
        catch (const std::out_of_range &)
        {
            rejected = true;
        }
        check(rejected, "Invalid player must be rejected");
    }

    void horizontalLatch(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        board->output(0x3f, 0x05); // TH A e B como saídas baixas.
        check((board->input(0xdd) & 0xc0) == 0, "TH output levels must be visible");
        board->runCycles(40);
        const auto expected = board->vdp().horizontalCounter();
        board->output(0x01, 0xff); // Espelho, transição para entradas altas.
        check(board->input(0x7f) == expected, "TH rising edge must latch H counter");
        board->runCycles(40);
        for (unsigned port = 0x41; port <= 0x7f; port += 2)
            check(board->input(static_cast<std::uint16_t>(port)) == expected, "H counter mirrors must retain latch");
        board->output(0x3f, 0xff);
        check(board->input(0x7f) == expected, "No transition must preserve the latch");
        board->output(0x3f, 0xdd); // Somente TH A baixa.
        check(board->input(0x7f) == expected && (board->input(0xdd) & 0xc0) == 0x80,
              "Falling TH must not latch and must affect only port A");
        const auto saved = board->saveState();
        board->output(0x3f, 0xff);
        const auto next = board->input(0x7f);
        check(next != expected, "A subsequent edge must capture the current counter");
        board->loadState(saved);
        check(board->input(0x7f) == expected && (board->input(0xdd) & 0xc0) == 0x80,
              "Save state must restore both latch and TH output");
        board->output(0x3f, 0xff);
        check(board->input(0x7f) == next, "Restored TH must capture the same counter on its next edge");
        board->output(0x3f, 0x77); // Somente TH B baixa.
        board->runCycles(40);
        const auto portB = board->vdp().horizontalCounter();
        board->output(0x3f, 0xff);
        check(board->input(0x7f) == portB, "Port B TH must also latch the counter");
    }

    void frameInterrupt()
    {
        std::vector<std::uint8_t> rom(0x100);
        // Habilita VINT em R1 e aguarda em HALT com IM 1.
        const std::uint8_t code[] = {0x3e, 0x20, 0xd3, 0xbf, 0x3e, 0x81, 0xd3, 0xbf, 0xed, 0x56, 0xfb, 0x76};
        // IN A,($BF); LD ($C000),A; HALT.
        const std::uint8_t handler[] = {0xdb, 0xbf, 0x32, 0, 0xc0, 0x76};
        std::copy(std::begin(code), std::end(code), rom.begin());
        std::copy(std::begin(handler), std::end(handler), rom.begin() + 0x38);
        auto board = std::make_unique<Core::MasterSystem>(Core::Cartridge("vblank.sms", std::move(rom), "vblank.sms"));

        constexpr auto vblankLine = Core::SegaVDP::Height * Core::SegaVDP::DotsPerLine * 2;
        constexpr auto statusCycle = (vblankLine + 318 * 2) / 3;
        board->runCycles(statusCycle - 1);
        check(board->cpu().state().halted && !board->cpu().state().irq && !(board->input(0xbf) & 0x80),
              "The status port must return VBlank clear before the latch");
        board->runCycles(1);
        check(board->cpu().state().halted && !board->cpu().state().irq && (board->vdp().state().status & 0x80),
              "VBlank status must be readable one CPU cycle before INT");
        const auto saved = board->saveState();

        board->runCycles(1);
        check(board->cpu().state().irq, "Unread VBlank must assert INT");
        board->runCycles(64);
        check(board->readMemory(0xc000) == 0x80 && !board->cpu().state().irq,
              "The Z80 must leave HALT, read the VBlank flag and acknowledge INT");

        board->loadState(saved);
        check(board->input(0xbf) == 0x80, "A status read in the gap must acknowledge VBlank");
        board->runCycles(64);
        check(board->cpu().state().halted && board->cpu().state().pc == sizeof code &&
                  !board->cpu().state().irq && board->readMemory(0xc000) == 0,
              "Acknowledging VBlank before INT must leave the Z80 in its original HALT");
    }

    void lightPhaser(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        board->setInput({mask(Button::Up) | mask(Button::B), 0});
        board->setLightPhaser(true, 80, 20, true);
        check((board->input(0xdc) & 0x3f) == 0x2f, "Phaser replaces port A with active-low trigger");
        check((board->input(0xdd) & 0x40) != 0, "Sensor starts dark");
        board->output(0xbf, 0x04);
        board->output(0xbf, 0x80); // A cena de teste usa o backdrop da CRAM do Mode 4.
        board->output(0xbf, 0x10);
        board->output(0xbf, 0xc0);
        board->output(0xbe, 0x3f); // Bright backdrop.
        bool detected = false;
        for (unsigned i = 0; i < board->cyclesPerFrame(); ++i)
        {
            board->runCycles(1);
            if (!(board->input(0xdd) & 0x40))
            {
                detected = true;
                check(board->input(0x7f) >= 57 && board->input(0x7f) <= 62, "Light latches the compensated horizontal beam position");
                const auto latch = board->input(0x7f);
                board->runCycles(20);
                check(!(board->input(0xdd) & 0x40) && board->input(0x7f) == latch,
                      "Sensor pulse remains readable without relatching");
                board->runCycles(50);
                check(board->input(0xdd) & 0x40, "Sensor pulse expires after the beam passes");
                break;
            }
        }
        check(detected, "Bright raster must activate sensor near aim");
        board->setLightPhaser(false, -1, -1, false);
        check((board->input(0xdd) & 0x40) && (board->input(0xdc) & 0x3f) == 0x1e,
              "Disabling Phaser restores controller and releases sensor");
    }

    void audio(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        for (unsigned port = 0x40; port <= 0x7f; ++port)
        {
            board->output(static_cast<std::uint16_t>(0xa500 | port), static_cast<std::uint8_t>(0x80 | (port & 15)));
            check((board->psg().state().tone[0] & 15) == (port & 15), "SMS PSG port mirrors");
        }
        board->output(0x7f, 0x80 | 14);
        board->output(0x7f, 15);
        board->output(0x7f, 0x90);
        board->output(0x7f, 0xbf);
        board->output(0x7f, 0xdf);
        board->output(0x7f, 0xff);
        const auto clocks = board->psg().state().clocks;
        board->runCycles(5000);
        check(board->psg().state().clocks == clocks + 5000 && !board->cpu().state().wait, "SMS PSG follows CPU clock without WAIT");
        std::vector<float> samples;
        board->takeAudio(samples);
        check(!samples.empty() && std::any_of(samples.begin(), samples.end(), [](float value)
                                              { return value != 0; }),
              "SMS produces PCM");
        Common::AudioSettings settings;
        settings.channels[0].enabled = false;
        board->setAudioSettings(settings);
        check(board->psg().level() == 0, "SMS respects individual channel mute");
        settings.channels[0].enabled = true;
        settings.master.enabled = false;
        board->setAudioSettings(settings);
        board->runCycles(5000);
        board->takeAudio(samples);
        check(!samples.empty() && std::all_of(samples.begin(), samples.end(), [](float value)
                                              { return value == 0; }),
              "SMS respects master mute");
    }

    void videoStartup(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        auto reg = [&](unsigned index, unsigned value)
        {
            board->output(0xbf, static_cast<std::uint8_t>(value));
            board->output(0xbf, static_cast<std::uint8_t>(0x80 | index));
        };
        auto write = [&](unsigned address, unsigned code, std::initializer_list<std::uint8_t> bytes)
        {
            board->output(0xbf, static_cast<std::uint8_t>(address));
            board->output(0xbf, static_cast<std::uint8_t>((code << 6) | (address >> 8)));
            for (auto byte : bytes)
                board->output(0xbe, byte);
        };
        reg(0, 4);
        reg(1, 0x40);
        reg(2, 0x0f);
        // Tile 257 requires address bits controlled by both R3 and R4.
        write(0x2020, 1, {0x80, 0, 0x80, 0});
        write(0x3800, 1, {1, 1});
        write(5, 3, {3});
        board->runCycles(board->cyclesPerFrame());
        const auto *pixel = board->videoFrame().rgba;
        check(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0,
              "SMS startup must allow tiles to use all pattern address bits");
        reg(3, 0);
        reg(4, 0);
        board->runCycles(board->cyclesPerFrame());
        pixel = board->videoFrame().rgba;
        check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0,
              "Games must still be able to program SMS1 pattern masks");
    }

    void graphicsI()
    {
        std::vector<std::uint8_t> rom{0xf3}; // DI.
        const auto output = [&](std::uint8_t port, std::uint8_t value)
        {
            rom.insert(rom.end(), {0x3e, value, 0xd3, port}); // LD A,n; OUT (port),A.
        };
        const auto reg = [&](unsigned index, std::uint8_t value)
        {
            output(0xbf, value);
            output(0xbf, 0x80 | index);
        };
        const auto write = [&](unsigned address, std::initializer_list<std::uint8_t> bytes)
        {
            output(0xbf, address & 0xff);
            output(0xbf, 0x40 | (address >> 8));
            for (auto byte : bytes)
                output(0xbe, byte);
        };
        reg(0, 0);
        reg(2, 0x0e);
        reg(3, 0x80);
        reg(4, 1);
        reg(5, 0x7f);
        write(0x3800, {8});
        write(0x0840, {0x80});
        write(0x2001, {0x24});
        write(0x3f80, {0xd0});
        reg(1, 0x40);
        rom.push_back(0x76); // HALT.
        auto board = std::make_unique<Core::MasterSystem>(Core::Cartridge("graphics-i.sms", std::move(rom), "graphics-i.sms"));
        board->runCycles(2 * board->cyclesPerFrame());
        const auto *pixel = board->videoFrame().rgba;
        check(board->cpu().state().halted && pixel[0] == 0 && pixel[1] == 170 && pixel[2] == 0 &&
                  pixel[4] == 0 && pixel[5] == 0 && pixel[6] == 85,
              "A Z80 ROM must render Graphics I through the SMS VDP ports");
    }

    void turbo(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Core::MasterSystem>(::R2SEGA8::Tests::loadCartridge(path));
        board->setInput({mask(Button::TurboA), mask(Button::TurboB)});
        constexpr auto frameTicks = Core::SegaVDP::MasterTicksPerFrame;
        constexpr auto activeTicks = Core::SegaVDP::Height * Core::SegaVDP::DotsPerLine * 2;
        constexpr auto boundary = (2ULL * frameTicks + activeTicks) / 3;
        board->runCycles(boundary - 1);
        check((board->input(0xdc) & 0x10) && (board->input(0xdd) & 8), "Turbo starts released");
        board->runCycles(1);
        check(board->videoFrame().number == 3 && !(board->input(0xdc) & 0x10) && !(board->input(0xdd) & 8),
              "Turbo activates at the third emulated frame");
        board->runCycles(frameTicks / 3);
        check((board->input(0xdc) & 0x10) && (board->input(0xdd) & 8), "Turbo releases at the next frame");
        board->setButtons(0, mask(Button::A) | mask(Button::TurboA));
        check(!(board->input(0xdc) & 0x10), "Normal button must override turbo release");
    }
}

int main()
{
    try
    {
        Fixture fixture;
        controls(fixture.path);
        horizontalLatch(fixture.path);
        frameInterrupt();
        lightPhaser(fixture.path);
        audio(fixture.path);
        videoStartup(fixture.path);
        graphicsI();
        turbo(fixture.path);
        std::cout << "Master System controller ports, CPU, Pause NMI and turbo passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}