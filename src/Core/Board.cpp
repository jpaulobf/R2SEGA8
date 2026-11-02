#include "Board.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Core
{
    Board::Board(Common::SystemType type, Cartridge cartridge, std::size_t ramSize)
        : cartridge_(std::move(cartridge)), ram_(ramSize, 0), type_(type), cpu_(*this) {}

    std::uint64_t Board::runCycles(std::uint64_t budget)
    {
        if (!hasMemoryMap())
            throw std::logic_error("CPU execution requires an implemented memory map.");
        if (!budget)
            return 0;
        if (cpuMultiplierPercent_ == 100)
            return cpu_.runCycles(budget);
        const auto phase = ((cpu_.state().cycles % cpuMultiplierPercent_) * 100) % cpuMultiplierPercent_;
        const auto cpuCycles = (budget * cpuMultiplierPercent_ + 99 - phase) / 100;
        cpu_.runCycles(cpuCycles);
        return budget;
    }

    void Board::setCPUOverclock(bool enabled, float multiplier)
    {
        if (!std::isfinite(multiplier))
            multiplier = 1.0f;
        cpuMultiplierPercent_ = enabled ? static_cast<unsigned>(std::lround(std::clamp(multiplier, 1.0f, 2.0f) * 100)) : 100;
    }

    bool Board::peripheralClockDue() const
    {
        return cpuMultiplierPercent_ == 100 ||
               ((cpu_.state().cycles % cpuMultiplierPercent_) * 100) % cpuMultiplierPercent_ < 100;
    }

    std::uint8_t Board::input(std::uint16_t) { return 0xff; }
    void Board::output(std::uint16_t, std::uint8_t) {}

    std::uint8_t Board::readMemory(std::uint16_t) const
    {
        throw std::logic_error("Memory mapping is not implemented for this system yet.");
    }

    void Board::writeMemory(std::uint16_t, std::uint8_t)
    {
        throw std::logic_error("Memory mapping is not implemented for this system yet.");
    }
}