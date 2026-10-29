#include "Engine.h"
#include "Board.h"
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
        R2SEGA8::Window::Window window([&](R2SEGA8::Core::Cartridge cartridge, R2SEGA8::Common::SystemType type)
                                       {
            auto next = R2SEGA8::Systems::createBoard(type, std::move(cartridge));
            board = std::move(next);
            cycleRemainder = 0;
            const auto &loaded = board->cartridge();
            const auto path = loaded.path().u8string();
            std::printf("System: %s\nFile: %s\nROM: %zu bytes | System RAM: %zu bytes\n",
                        R2SEGA8::Common::systemName(board->type()), path.c_str(), loaded.rom().size(), board->ram().size());
            if (loaded.header())
                std::printf("Sega header: 0x%04X | Region: %X | Size code: %X\n",
                            static_cast<unsigned>(loaded.header()->offset), static_cast<unsigned>(loaded.header()->region),
                            static_cast<unsigned>(loaded.header()->sizeCode));
            if (board->hasMemoryMap())
                std::puts("SG-1000: Z80 enabled; 0000-BFFF cartridge; C000-FFFF mirrored 1 KiB RAM. Video, audio, input, cartridge RAM and banking are pending.");
            else
                std::puts("ROM loaded; system memory mapping is not implemented yet.");
            std::fflush(stdout); });
        window.open();
        while (true)
        {
            const auto frameStart = Clock::now();
            if (!window.processEvents())
                break;
            if (board && board->clockFrequency())
            {
                // Orçamento provisório por quadro; preserva a fração sem recuperar quadros atrasados.
                cycleRemainder += board->clockFrequency();
                const auto budget = cycleRemainder / targetFPS;
                cycleRemainder %= targetFPS;
                if (budget)
                    board->runCycles(budget);
            }
            window.render(board.get());
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