#include "Systems/FrameCounter.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    // Interrompe a validação com a propriedade da medição que não foi atendida.
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
        using Counter = R2SEGA8::Systems::FrameCounter;
        using namespace std::chrono;
        const auto start = Counter::Clock::time_point{};
        Counter counter(start);
        check(counter.fps() == 0, "No emulated frames must report zero FPS");
        for (unsigned i = 1; i < 30; ++i)
            counter.record(start + milliseconds(i * 10));
        check(std::abs(counter.fps() - 100) < 1e-9, "An incomplete sampling window must publish the current average immediately");
        counter.record(start + milliseconds(500));
        check(std::abs(counter.fps() - 60) < 1e-9, "Thirty emulated frames in half a second must report 60 FPS");
        for (unsigned i = 1; i <= 60; ++i)
            counter.record(start + milliseconds(500) + nanoseconds(i * 500000000ull / 60));
        check(std::abs(counter.fps() - 120) < 1e-9, "FPS must count every emulated frame even when presentations are skipped");
        counter.reset(start + seconds(3));
        check(counter.fps() == 0, "ROM changes must reset both the value and sampling window");
        for (unsigned i = 1; i < 300; ++i)
            counter.record(start + seconds(3) + milliseconds(i));
        counter.record(start + milliseconds(3500));
        check(std::abs(counter.fps() - 600) < 1e-9, "Fast-forward must expose emulated throughput without a 60 FPS clamp");
        counter.record(start + milliseconds(13500));
        check(std::abs(counter.fps() - 0.1) < 1e-9, "A long host pause must use the actual elapsed time");
        counter.reset(start);
        counter.record(start + milliseconds(10));
        check(std::abs(counter.fps() - 100) < 1e-9, "The first completed frame must publish FPS without a warm-up delay");
        counter.record(start + milliseconds(20));
        counter.record(start + milliseconds(21));
        check(std::abs(counter.fps() - 3000.0 / 21) < 1e-9,
              "The first accelerated frame must immediately update the average using the existing count");
        counter.record(start + milliseconds(31));
        check(std::abs(counter.fps() - 4000.0 / 31) < 1e-9,
              "Returning to normal speed must immediately update the same window without zeroing FPS");
        counter.reset(start);
        counter.record(start);
        check(counter.fps() == 0 && std::isfinite(counter.fps()), "A zero-duration sample must not divide by zero");
        counter.record(start + milliseconds(1));
        check(std::abs(counter.fps() - 2000) < 1e-9, "Frames sharing a clock sample must remain in the current count");
        counter.reset(start);
        counter.record(start + milliseconds(10));
        counter.record(start + milliseconds(20));
        counter.excludePause(seconds(10));
        check(std::abs(counter.fps() - 100) < 1e-9, "Pause must preserve the displayed FPS and current frame count");
        counter.record(start + seconds(10) + milliseconds(30));
        check(std::abs(counter.fps() - 100) < 1e-9, "Resuming must exclude paused wall time from the existing FPS window");
        std::cout << "Emulated FPS sampling, fast-forward counting, reset and long-pause tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}