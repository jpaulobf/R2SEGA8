#include "Core/CPU/Z80/Z80.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

// Executor opcional de testes CP/M; não inclui ROMs comerciais nem binários de terceiros.
namespace
{
    struct Bus final : R2SEGA8::Core::Z80Bus
    {
        std::array<std::uint8_t, 65536> memory{};
        std::uint8_t readMemory(std::uint16_t address) const override { return memory[address]; }
        void writeMemory(std::uint16_t address, std::uint8_t value) override { memory[address] = value; }
        std::uint8_t input(std::uint16_t) override { return 0xff; }
        void output(std::uint16_t, std::uint8_t) override {}
    };
}

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2 || argc > 3)
            throw std::runtime_error("Usage: z80_exerciser program.com [instruction-limit]");
        std::ifstream file(argv[1], std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot open exerciser.");
        const std::string program{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (program.empty() || program.size() > 0xff00)
            throw std::runtime_error("Invalid CP/M binary size.");
        Bus bus;
        std::copy(program.begin(), program.end(), bus.memory.begin() + 0x100);
        bus.memory[5] = 0xc9; // Retorna de BDOS após o executor atender à chamada.
        R2SEGA8::Core::Z80 cpu(bus);
        auto state = cpu.state();
        state.pc = 0x100;
        cpu.setState(state);
        const auto limit = argc == 3 ? std::stoull(argv[2]) : 100000000000ULL;
        std::string output;
        std::string transcript;
        std::uint64_t instructions = 0;
        while (cpu.state().pc && instructions++ < limit)
        {
            const auto &s = cpu.state();
            if (s.pc == 5)
            {
                if (s.c == 2)
                    output += static_cast<char>(s.e);
                else if (s.c == 9)
                {
                    auto address = static_cast<std::uint16_t>((s.d << 8) | s.e);
                    unsigned length = 0;
                    while (bus.memory[address] != '$' && length++ < 65536)
                        output += static_cast<char>(bus.memory[address++]);
                    if (length > 65536)
                        throw std::runtime_error("Unterminated BDOS string.");
                }
                else
                    throw std::runtime_error("Unsupported BDOS call.");
                std::cout << output << std::flush;
                transcript += output;
                output.clear();
            }
            cpu.step();
        }
        std::cout << output << "\nInstructions: " << instructions << "; T-states: " << cpu.state().cycles << '\n';
        if (cpu.state().pc)
            throw std::runtime_error("Exerciser instruction limit reached.");
        if (transcript.find("ERROR") != std::string::npos || transcript.find("Tests complete") == std::string::npos)
            throw std::runtime_error("Exerciser reported failures or did not report completion.");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}