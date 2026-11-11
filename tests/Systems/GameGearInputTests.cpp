#include "../Support/CartridgeFixture.h"
#include "Core/Systems/GameGear/GameGear.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace R2SEGA8;
    using Common::Button;
    using Common::mask;
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("r2sega8-gg-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gg");
        Fixture()
        {
            // Lê Start e os botões, grava em RAM e para sem habilitar interrupções.
            const std::uint8_t code[] = {0xf3, 0xdb, 0x00, 0x32, 0x00, 0xc0, 0xdb, 0xdc, 0x32, 0x01, 0xc0, 0x76};
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(code), sizeof(code));
            file.close();
            check(bool(file), "Unable to write Game Gear ROM fixture");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };
}

int main()
{
    try
    {
        Fixture fixture;
        auto board = std::make_unique<Core::GameGear>(::R2SEGA8::Tests::loadCartridge(fixture.path));
        check(board->input(0) == 0xdf && board->input(0xdc) == 0xff && board->input(0xdd) == 0xff,
              "Released controls must expose an overseas NTSC Game Gear");
        board->setInput({mask(Button::Up) | mask(Button::A) | mask(Button::Start), mask(Button::Down) | mask(Button::Start)});
        check(board->input(0x1200) == 0x5f && board->input(0xdc) == 0xee && board->input(0xdd) == 0xff,
              "Built-in controls are active-low and do not expose player two as an external connector");
        for (unsigned port = 0xc0; port <= 0xff; ++port)
            check(board->input(static_cast<std::uint16_t>(0xa500 | port)) == (port & 1 ? 0xff : 0xee),
                  "Game Gear controller ports must mirror correctly");
        board->runCycles(256);
        check(board->cpu().state().halted && board->readMemory(0xc000) == 0x5f && board->readMemory(0xc001) == 0xee,
              "CPU must read Start and the controller from their respective ports");
        board->setInput({});
        check(board->input(0) == 0xdf && board->input(0xdc) == 0xff, "Clearing input must release controls");
        board->setButtons(1, mask(Button::Start) | mask(Button::B));
        check(board->input(0) == 0xdf && board->input(0xdc) == 0xff && board->input(0xdd) == 0xff,
              "Player two must not control the built-in pad or Start");
        board->setButtons(0, mask(Button::Select) | mask(Button::TurboA) | mask(Button::TurboB));
        check(board->input(0) == 0xdf && board->input(0xdc) == 0xff, "Select is unused and turbo starts released");
        constexpr auto frameTicks = Core::SegaVDP::MasterTicksPerFrame;
        constexpr auto activeTicks = Core::SegaVDP::Height * Core::SegaVDP::DotsPerLine * 2;
        constexpr auto boundary = (2ULL * frameTicks + activeTicks) / 3;
        board->runCycles(boundary - board->cpu().state().cycles - 1);
        check((board->input(0xdc) & 0x30) == 0x30, "Turbo must remain released until the third frame");
        board->runCycles(1);
        check(board->videoFrame().number == 3 && (board->input(0xdc) & 0x30) == 0,
              "Turbo must activate at the emulated frame boundary");
        board->runCycles(frameTicks / 3);
        check((board->input(0xdc) & 0x30) == 0x30, "Turbo must release at the next frame");
        board->setButtons(0, mask(Button::A) | mask(Button::TurboA));
        check((board->input(0xdc) & 0x10) == 0, "Normal button must override turbo release");
        bool rejected = false;
        try
        {
            board->setButtons(2, 0);
        }
        catch (const std::out_of_range &)
        {
            rejected = true;
        }
        check(rejected, "Invalid player index must be rejected");
        std::cout << "Game Gear controls, Start and turbo passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}