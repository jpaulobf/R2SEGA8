#include "Engine.h"
#include "Board.h"
#include "Input.h"
#include "Window/Window.h"
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>

int R2SEGA8::Systems::Engine::run()
{
    try
    {
        std::unique_ptr<R2SEGA8::Systems::Board> board;
        using Clock = std::chrono::steady_clock;
        constexpr unsigned targetFPS = 60;
        const auto frameDuration = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / targetFPS));
        std::uint64_t cycleRemainder = 0;
        bool running = true, inputEnabled = false;
        R2SEGA8::Systems::Input input;
        R2SEGA8::Window::Callbacks callbacks;
        callbacks.openRom = [&](const std::filesystem::path &file, R2SEGA8::Common::SystemType type)
        {
            auto next = R2SEGA8::Systems::createBoard(type, R2SEGA8::Core::Cartridge(file));
            board = std::move(next);
            cycleRemainder = 0;
            input.clear();
            const auto &loaded = board->cartridge();
            const auto path = loaded.path().u8string();
            std::printf("System: %s\nFile: %s\nROM: %zu bytes | System RAM: %zu bytes\n", R2SEGA8::Common::systemName(board->type()),
                        path.c_str(), loaded.rom().size(), board->ram().size());
            if (!loaded.ram().empty())
                std::printf("Cartridge RAM: %zu bytes (The Castle profile)\n", loaded.ram().size());
            if (loaded.header())
                std::printf("Sega header: 0x%04X | Region: %X | Size code: %X\n", static_cast<unsigned>(loaded.header()->offset),
                            static_cast<unsigned>(loaded.header()->region), static_cast<unsigned>(loaded.header()->sizeCode));
            if (board->hasMemoryMap())
                std::puts("SG-1000: Z80 and TMS9918 video enabled (Graphics I/II, Text, Multicolor and sprites). Controllers enabled. "
                          "Audio and cartridge banking are pending.");
            else
                std::puts("ROM loaded; system memory mapping is not implemented yet.");
            std::fflush(stdout);
        };
        callbacks.memory = [&]() -> R2SEGA8::Window::MemoryView
        {
            if (!board)
                return {};
            const auto &rom = board->cartridge().rom();
            const auto &ram = board->ram();
            return {{rom.data(), rom.size()}, {ram.data(), ram.size()}};
        };
        callbacks.exit = [&]()
        { running = false; };
        callbacks.frame = [&]() -> R2SEGA8::Window::FrameView
        {
            if (!board)
                return {};
            const auto frame = board->videoFrame();
            return {frame.rgba, frame.width, frame.height, frame.pitch};
        };
        callbacks.inputEvent = [&](const SDL_Event &event, bool enabled)
        { input.handleEvent(event, enabled); };
        callbacks.inputAvailability = [&](bool enabled)
        {
            inputEnabled = enabled;
            if (!enabled)
            {
                input.clear();
                if (board)
                    board->setInput({});
            }
        };
        R2SEGA8::Window::Window window(std::move(callbacks), {256, 192});
        window.open();
        // A sessão termina antes de a janela chamar SDL_Quit, inclusive durante exceções.
        const auto stopInput = [](R2SEGA8::Systems::Input *session)
        { session->shutdown(); };
        const std::unique_ptr<R2SEGA8::Systems::Input, decltype(stopInput)> inputSession(&input, stopInput);
        if (!input.initialize())
            std::cerr << "Input: Gamepads unavailable: " << SDL_GetError() << '\n';
        while (running)
        {
            const auto frameStart = Clock::now();
            if (!window.processEvents())
                break;
            const auto buttons = input.update(inputEnabled);
            if (board)
                board->setInput(buttons);
            if (board && board->clockFrequency())
            {
                // Orçamento provisório por quadro; preserva a fração sem recuperar quadros atrasados.
                cycleRemainder += board->clockFrequency();
                const auto budget = cycleRemainder / targetFPS;
                cycleRemainder %= targetFPS;
                if (budget)
                    board->runCycles(budget);
            }
            window.render();
            // Cede a CPU ao sistema operacional até completar o intervalo de 60 FPS.
            std::this_thread::sleep_until(frameStart + frameDuration);
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "R2SEGA8: " << error.what() << '\n';
        return 1;
    }
}