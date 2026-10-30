#include "Board.h"
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
        return cpu_.runCycles(budget);
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