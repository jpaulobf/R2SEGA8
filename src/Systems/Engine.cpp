#include "Engine.h"
#include "Board.h"
#include "Window/Window.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <utility>

int R2SEGA8::Systems::Engine::run()
{
    try
    {
        std::unique_ptr<R2SEGA8::Systems::Board> board;
        using Clock = std::chrono::steady_clock;
        auto lastTick = Clock::now();
        double cycleBalance = 0;
        R2SEGA8::Window::Window window([&](R2SEGA8::Core::Cartridge cartridge, R2SEGA8::Common::SystemType type)
                                       {
            auto next = R2SEGA8::Systems::createBoard(type, std::move(cartridge));
            board = std::move(next);
            lastTick = Clock::now();
            cycleBalance = 0;
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
        while (window.processEvents())
        {
            const auto now = Clock::now();
            // Descarta pausas longas do hospedeiro para limitar a recuperação e manter a interface responsiva.
            const auto elapsed = std::min(now - lastTick, std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(50)));
            lastTick = now;
            if (board && board->clockFrequency())
            {
                cycleBalance += std::chrono::duration<double>(elapsed).count() * board->clockFrequency();
                if (cycleBalance >= 1)
                    cycleBalance -= static_cast<double>(board->runCycles(static_cast<std::uint64_t>(cycleBalance)));
            }
            window.render(board.get());
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "R2SEGA8: " << error.what() << '\n';
        return 1;
    }
}