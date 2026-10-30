#include "Systems/FramePacer.h"
#include "Systems/SG1000/SG1000.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    // Interrompe a validação com o contrato de temporização que deixou de ser atendido.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        using R2SEGA8::Systems::FramePacer;
        constexpr unsigned frequency = 3579545, cycles = R2SEGA8::Core::TMS9918::MasterTicksPerFrame / 3;
        static_assert(cycles == 59736, "NTSC frame duration");
        check(std::abs(static_cast<double>(frequency) / cycles - 59.92274340431231) < 1e-10,
              "SG-1000 frame rate must follow the crystal and raster, not rounded NTSC television FPS");
        const auto start = FramePacer::Clock::time_point{};
        FramePacer pacer;
        pacer.reset(frequency, cycles, start);
        for (std::uint64_t frame = 1; frame <= 100000; ++frame)
        {
            pacer.advance();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(pacer.deadline() - start).count();
            check(static_cast<std::uint64_t>(elapsed) == frame * cycles * 1000000000ull / frequency,
                  "Frame deadlines must preserve fractional nanoseconds without drift");
        }
        pacer.reset(60, 1, start);
        for (unsigned frame = 0; frame < 60; ++frame)
            pacer.advance();
        check(pacer.deadline() - start == std::chrono::seconds(1), "Idle UI timing must span exactly one second");
        const auto later = start + std::chrono::seconds(50);
        pacer.reset(frequency, cycles, later);
        check(pacer.deadline() == later, "ROM/reset must discard the previous schedule");
        pacer.advance();
        check(pacer.deadline() > later, "New schedule must start at the next native frame");
        bool rejected = false;
        try
        {
            pacer.reset(0, cycles, start);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid timing accepted");
        std::cout << "Native frame pacing tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}