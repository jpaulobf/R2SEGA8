#include "Core/CPU/Z80/Z80.h"
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Protocolo de texto gerado por RunZ80Vectors.py, evitando uma dependência JSON no núcleo.
namespace
{
    using R2SEGA8::Core::Z80;
    struct Port
    {
        unsigned address, value;
        char direction;
    };
    struct Bus final : R2SEGA8::Core::Z80Bus
    {
        std::array<std::uint8_t, 65536> memory{};
        std::vector<Port> ports;
        std::vector<std::pair<unsigned, unsigned>> writes;
        std::vector<R2SEGA8::Core::Z80Pins> trace;
        // Guarda cada T-state para comparação com os sinais do vetor independente.
        void clock(const R2SEGA8::Core::Z80Pins &pins) override { trace.push_back(pins); }
        std::size_t portIndex = 0;
        bool mismatch = false;
        std::uint8_t readMemory(std::uint16_t address) const override { return memory[address]; }
        void writeMemory(std::uint16_t address, std::uint8_t value) override
        {
            memory[address] = value;
            writes.emplace_back(address, value);
        }
        std::uint8_t input(std::uint16_t address) override
        {
            if (portIndex >= ports.size())
            {
                mismatch = true;
                return 0xff;
            }
            const auto port = ports[portIndex++];
            mismatch |= port.address != address || port.direction != 'r';
            return static_cast<std::uint8_t>(port.value);
        }
        void output(std::uint16_t address, std::uint8_t value) override
        {
            if (portIndex >= ports.size())
            {
                mismatch = true;
                return;
            }
            const auto port = ports[portIndex++];
            mismatch |= port.address != address || port.value != value || port.direction != 'w';
        }
    };
    Z80::State readState(std::istream &in)
    {
        Z80::State s;
        unsigned af, bc, de, hl;
        auto byte = [&](std::uint8_t &value)
        { unsigned number; in >> number; value = static_cast<std::uint8_t>(number); };
        in >> s.pc >> s.sp;
        byte(s.a);
        byte(s.f);
        byte(s.b);
        byte(s.c);
        byte(s.d);
        byte(s.e);
        byte(s.h);
        byte(s.l);
        byte(s.i);
        byte(s.r);
        in >> s.ix >> s.iy >> af >> bc >> de >> hl >> s.wz >> s.iff1 >> s.iff2;
        byte(s.im);
        byte(s.eiDelay);
        in >> s.p;
        byte(s.q);
        s.aAlt = af >> 8;
        s.fAlt = af & 255;
        s.bAlt = bc >> 8;
        s.cAlt = bc & 255;
        s.dAlt = de >> 8;
        s.eAlt = de & 255;
        s.hAlt = hl >> 8;
        s.lAlt = hl & 255;
        return s;
    }
}

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    unsigned count = 0, failed = 0;
    std::string name;
    Bus bus;
    Z80 cpu(bus);
    while (std::cin >> name)
    {
        bus.memory.fill(0);
        bus.ports.clear();
        bus.writes.clear();
        bus.trace.clear();
        bus.portIndex = 0;
        bus.mismatch = false;
        const auto initial = readState(std::cin);
        unsigned size, address, value;
        std::cin >> size;
        for (unsigned i = 0; i < size; ++i)
        {
            std::cin >> address >> value;
            bus.memory[address] = static_cast<std::uint8_t>(value);
        }
        const auto expected = readState(std::cin);
        std::vector<std::pair<unsigned, unsigned>> memory;
        std::cin >> size;
        for (unsigned i = 0; i < size; ++i)
        {
            std::cin >> address >> value;
            memory.emplace_back(address, value);
        }
        unsigned cycles;
        std::cin >> cycles >> size;
        for (unsigned i = 0; i < size; ++i)
        {
            Port port{};
            std::cin >> port.address >> port.value >> port.direction;
            bus.ports.push_back(port);
        }
        std::vector<std::pair<unsigned, unsigned>> writes;
        std::cin >> size;
        for (unsigned i = 0; i < size; ++i)
        {
            std::cin >> address >> value;
            writes.emplace_back(address, value);
        }
        struct Sample
        {
            int address, data;
            unsigned signals;
        };
        std::vector<Sample> samples;
        std::cin >> size;
        for (unsigned i = 0; i < size; ++i)
        {
            Sample sample{};
            std::cin >> sample.address >> sample.data >> sample.signals;
            samples.push_back(sample);
        }
        if (!std::cin)
        {
            std::cerr << "Malformed vector " << name << '\n';
            return 2;
        }
        cpu.setState(initial);
        const auto actualCycles = cpu.step();
        const auto &actual = cpu.state();
        std::ostringstream errors;
        auto compare = [&](const char *label, unsigned a, unsigned e)
        { if (a != e) errors << ' ' << label << '=' << a << " expected " << e; };
#define CHECK(field) compare(#field, actual.field, expected.field)
        CHECK(pc);
        CHECK(sp);
        CHECK(a);
        CHECK(f);
        CHECK(b);
        CHECK(c);
        CHECK(d);
        CHECK(e);
        CHECK(h);
        CHECK(l);
        CHECK(i);
        CHECK(r);
        CHECK(ix);
        CHECK(iy);
        CHECK(aAlt);
        CHECK(fAlt);
        CHECK(bAlt);
        CHECK(cAlt);
        CHECK(dAlt);
        CHECK(eAlt);
        CHECK(hAlt);
        CHECK(lAlt);
        CHECK(wz);
        CHECK(iff1);
        CHECK(iff2);
        CHECK(im);
        CHECK(eiDelay);
        CHECK(p);
        CHECK(q);
#undef CHECK
        compare("cycles", actualCycles, cycles);
        compare("ports", bus.mismatch || bus.portIndex != bus.ports.size(), 0);
        compare("write-sequence", bus.writes != writes, 0);
        using Pins = R2SEGA8::Core::Z80Pins;
        for (std::size_t t = 0; t < samples.size() && t < bus.trace.size(); ++t)
        {
            const auto &wanted = samples[t];
            const auto &got = bus.trace[t];
            unsigned signals = got.signals & (Pins::Read | Pins::Write | Pins::Memory | Pins::IO);
            if (got.signals & Pins::Refresh)
                signals &= ~Pins::Memory;
            if ((wanted.address >= 0 && (!got.addressValid || got.address != wanted.address)) ||
                (wanted.data >= 0 && (!got.dataValid || got.data != wanted.data)) || wanted.signals != signals)
            {
                errors << " T" << t + 1 << " pins=" << got.address << ',' << (got.dataValid ? int(got.data) : -1) << ',' << signals
                       << " expected " << wanted.address << ',' << wanted.data << ',' << wanted.signals;
                break;
            }
        }
        for (const auto &entry : memory)
            compare("memory", bus.memory[entry.first], entry.second);
        const auto message = errors.str();
        if (!message.empty())
        {
            if (failed++ < 12)
                std::cout << name << ':' << message << '\n';
        }
        ++count;
    }
    std::cout << count << " vectors, " << failed << " failures\n";
    return failed ? 1 : 0;
}