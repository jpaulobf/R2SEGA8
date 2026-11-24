#include "Systems/FramePacer.h"
#include "Core/Systems/SG1000/SG1000.h"
#include "Core/VDP/SegaVDP/SegaVDP.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    using R2SEGA8::Systems::FramePacer;

    // Interrompe a validação com o contrato de temporização que deixou de ser atendido.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void checkPresentationCadence(unsigned frequency, unsigned cycles, unsigned refresh)
    {
        const auto start = FramePacer::Clock::time_point{};
        FramePacer pacer;
        pacer.reset(frequency, cycles, start);
        std::uint64_t frames = 0;
        bool repeated = false, batched = false;
        for (std::uint64_t refreshIndex = 0; refreshIndex <= 60 * refresh; ++refreshIndex)
        {
            const auto elapsed = std::chrono::nanoseconds(refreshIndex * 1000000000ull / refresh);
            const auto now = start + std::chrono::duration_cast<FramePacer::Clock::duration>(elapsed);
            unsigned due = 0;
            while (due < 4 && pacer.takeFrame(now))
                ++due;
            frames += due;
            repeated |= due == 0;
            batched |= due > 1;
        }
        const std::uint64_t expected = 1 + 60ull * frequency / cycles;
        check(frames == expected, "Presentation cadence must preserve the number of native frames");
        if (refresh * cycles > frequency)
            check(repeated, "A faster display must sometimes present without emulating a new frame");
        else
            check(batched, "A slower display must sometimes emulate several frames per presentation");
    }
}

int main()
{
    try
    {
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

        using R2SEGA8::Core::SegaVDP;
        constexpr unsigned palCycles = SegaVDP::DotsPerLine * 2 * SegaVDP::PalLinesPerFrame / 3;
        for (const unsigned refresh : {30, 50, 60, 120, 144})
        {
            checkPresentationCadence(frequency, cycles, refresh);
            checkPresentationCadence(3546893, palCycles, refresh);
        }
        pacer.reset(60, 1, start);
        check(pacer.takeFrame(start), "First frame must be due immediately after reset");
        check(!pacer.takeFrame(start), "Taking a frame must advance its deadline");
        const auto delayed = start + std::chrono::milliseconds(500);
        check(pacer.takeFrame(delayed), "A long stall must allow the current frame to run");
        check(!pacer.takeFrame(delayed), "A long stall must discard old deadlines");
        check(pacer.deadline() == delayed + std::chrono::nanoseconds(1000000000 / 60),
              "The schedule must resume at the native cadence after a stall");

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