#include "Core/CPU/Z80/Z80.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using R2SEGA8::Core::Z80;
    using Byte = std::uint8_t;
    struct Bus final : R2SEGA8::Core::Z80Bus
    {
        std::array<Byte, 65536> memory{};
        std::vector<std::pair<std::uint16_t, Byte>> writes, outputs;
        std::vector<std::uint16_t> inputs;
        Byte inputValue = 0xff, vector = 0xff;
        unsigned acknowledgements = 0, returns = 0;
        unsigned ticks = 0;
        bool tracing = false;
        std::vector<R2SEGA8::Core::Z80Pins> trace;
        mutable std::vector<unsigned> readTimes;
        std::vector<unsigned> writeTimes, inputTimes, outputTimes;
        Byte readMemory(std::uint16_t address) const override
        {
            if (tracing)
                readTimes.push_back(ticks + 1);
            return memory[address];
        }
        void writeMemory(std::uint16_t address, Byte value) override
        {
            memory[address] = value;
            writes.emplace_back(address, value);
            if (tracing)
                writeTimes.push_back(ticks + 1);
        }
        Byte input(std::uint16_t port) override
        {
            inputs.push_back(port);
            if (tracing)
                inputTimes.push_back(ticks + 1);
            return inputValue;
        }
        void output(std::uint16_t port, Byte value) override
        {
            outputs.emplace_back(port, value);
            if (tracing)
                outputTimes.push_back(ticks + 1);
        }
        Byte interruptAcknowledge() override
        {
            ++acknowledgements;
            return vector;
        }
        void reti() override { ++returns; }
        void clock(const R2SEGA8::Core::Z80Pins &pins) override
        {
            ++ticks;
            if (tracing)
                trace.push_back(pins);
        }
    };
    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    bool evenParity(unsigned value)
    {
        unsigned ones = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            ones += (value >> bit) & 1;
        return ones % 2 == 0;
    }
    unsigned sz(unsigned value) { return (value & 0xa8) | (value == 0 ? 0x40 : 0); }
    int signedByte(unsigned value) { return value < 128 ? int(value) : int(value) - 256; }

    void arithmetic()
    {
        Bus bus;
        Z80 cpu(bus);
        for (unsigned operation = 0; operation < 8; ++operation)
        {
            bus.memory[0] = static_cast<Byte>(0xc6 + operation * 8);
            for (unsigned a = 0; a < 256; ++a)
                for (unsigned value = 0; value < 256; ++value)
                    for (unsigned carry = 0; carry < 2; ++carry)
                    {
                        Z80::State state;
                        state.a = static_cast<Byte>(a);
                        state.f = static_cast<Byte>(carry);
                        cpu.setState(state);
                        bus.memory[1] = static_cast<Byte>(value);
                        check(cpu.step() == 7, "Immediate ALU timing");
                        unsigned expectedA, expectedF;
                        if (operation < 4 || operation == 7)
                        {
                            const bool subtract = operation == 2 || operation == 3 || operation == 7;
                            const int c = operation == 1 || operation == 3 ? int(carry) : 0;
                            const int result = subtract ? int(a) - int(value) - c : int(a) + int(value) + c;
                            const int signedResult = signedByte(a) + (subtract ? -signedByte(value) - c : signedByte(value) + c);
                            const unsigned out = unsigned(result) & 255;
                            const bool half = subtract ? int(a & 15) - int(value & 15) - c < 0 : (a & 15) + (value & 15) + c > 15;
                            expectedA = operation == 7 ? a : out;
                            expectedF = sz(out) | (subtract ? 2 : 0) | (result < 0 || result > 255 ? 1 : 0) |
                                        (half ? 16 : 0) | (signedResult < -128 || signedResult > 127 ? 4 : 0);
                            if (operation == 7)
                                expectedF = (expectedF & ~0x28) | (value & 0x28);
                        }
                        else
                        {
                            expectedA = operation == 4 ? a & value : operation == 5 ? a ^ value
                                                                                    : a | value;
                            expectedF = sz(expectedA) | (evenParity(expectedA) ? 4 : 0) | (operation == 4 ? 16 : 0);
                        }
                        if (cpu.state().a != expectedA || cpu.state().f != expectedF)
                            throw std::runtime_error("ALU mismatch op=" + std::to_string(operation) + " A=" + std::to_string(a) +
                                                     " operand=" + std::to_string(value) + " carry=" + std::to_string(carry));
                    }
        }
        for (unsigned value = 0; value < 256; ++value)
            for (unsigned carry = 0; carry < 2; ++carry)
                for (unsigned decrement = 0; decrement < 2; ++decrement)
                {
                    bus.memory[0] = static_cast<Byte>(4 + decrement);
                    Z80::State state;
                    state.b = static_cast<Byte>(value);
                    state.f = static_cast<Byte>(carry);
                    cpu.setState(state);
                    check(cpu.step() == 4, "INC/DEC timing");
                    const unsigned out = (value + (decrement ? 255 : 1)) & 255;
                    const bool half = decrement ? (value & 15) == 0 : (value & 15) == 15;
                    const unsigned flags = sz(out) | carry | (half ? 16 : 0) | (decrement ? 2 : 0) |
                                           (value == (decrement ? 128u : 127u) ? 4 : 0);
                    check(cpu.state().b == out && cpu.state().f == flags, "INC/DEC flags");
                }
    }

    void programsAndPrefixes()
    {
        Bus bus;
        Z80 cpu(bus);
        // Um programa pequeno verifica chamadas, pilha e a volta de FFFF para 0000.
        const Byte program[] = {0x31, 0x00, 0x00, 0x3e, 0x29, 0xcd, 0x10, 0x00, 0x32, 0x00, 0xc0, 0x76};
        std::copy(std::begin(program), std::end(program), bus.memory.begin());
        bus.memory[0x10] = 0xc6;
        bus.memory[0x11] = 0x13;
        bus.memory[0x12] = 0xc9;
        const unsigned timings[] = {10, 7, 17, 7, 10, 13, 4};
        for (auto cycles : timings)
            check(cpu.step() == cycles, "Program instruction timing");
        check(bus.memory[0xc000] == 0x3c && cpu.state().halted && cpu.state().sp == 0, "Program result/stack");
        check(bus.writes[0] == std::make_pair(std::uint16_t(0xffff), Byte(0)) &&
                  bus.writes[1] == std::make_pair(std::uint16_t(0xfffe), Byte(8)),
              "PUSH high byte first and wrapping");
        const auto pc = cpu.state().pc;
        check(cpu.step() == 4 && cpu.state().pc == pc, "HALT refresh without PC advance");

        Z80::State state;
        state.ix = 0;
        state.iy = 0x9000;
        state.h = 0x55;
        state.l = 0xaa;
        state.r = 0xfe;
        const Byte indexed[] = {0xdd, 0xfd, 0x66, 0xff, 0xdd, 0x74, 0xfe, 0xdd, 0xcb, 0xfe, 0x00};
        std::copy(std::begin(indexed), std::end(indexed), bus.memory.begin());
        bus.memory[0x8fff] = 0x81;
        cpu.setState(state);
        check(cpu.step() == 23 && cpu.state().h == 0x81 && cpu.state().iy == 0x9000 && cpu.state().r == 0x81,
              "Last DD/FD prefix wins; indexed LD H uses real H; R wraps only low seven bits");
        check(cpu.step() == 19 && bus.memory[0xfffe] == 0x81, "Negative IX displacement wraps");
        const auto before = cpu.state().r;
        check(cpu.step() == 23 && bus.memory[0xfffe] == 3 && cpu.state().b == 3 && cpu.state().r == Byte(before + 2),
              "DDCB memory/register result and non-M1 final opcode");

        bus.memory.fill(0xdd);
        cpu.reset();
        cpu.setIRQ(true);
        check(cpu.step() == 1024 && cpu.state().prefix == 0xdd && cpu.state().pc == 256, "Prefix stream yields");
        auto snapshot = cpu.state();
        snapshot.iff1 = true;
        snapshot.eiDelay = 1;
        cpu.setState(snapshot);
        bus.memory[256] = 0x21;
        bus.memory[257] = 0x34;
        bus.memory[258] = 0x12;
        check(cpu.step() == 10 && cpu.state().ix == 0x1234 && cpu.state().prefix == 0 && !cpu.state().eiDelay,
              "Prefix continuation restores decoder and does not accept IRQ mid-instruction");
        check(cpu.step() == 13 && cpu.state().pc == 0x38, "IRQ after completed prefix instruction");
        check(cpu.runCycles(0) == 0, "Zero cycle budget");
    }

    void arithmetic16AndBCD()
    {
        Bus bus;
        Z80 cpu(bus);
        std::uint32_t random = 0x5a801337;
        auto next = [&]
        { random = random * 1664525u + 1013904223u; return random >> 16; };
        for (unsigned operation = 0; operation < 3; ++operation)
            for (unsigned sample = 0; sample < 30000; ++sample)
            {
                const unsigned a = next(), b = next(), carry = sample & 1;
                Z80::State s;
                s.h = a >> 8;
                s.l = a & 255;
                s.b = b >> 8;
                s.c = b & 255;
                s.f = 0xc4 | carry;
                bus.memory[0] = operation == 0 ? 0x09 : 0xed;
                bus.memory[1] = operation == 1 ? 0x4a : 0x42;
                cpu.setState(s);
                check(cpu.step() == (operation == 0 ? 11u : 15u), "16-bit ALU cycles");
                const bool subtract = operation == 2;
                const int c = operation ? int(carry) : 0;
                const int result = subtract ? int(a) - int(b) - c : int(a) + int(b) + c;
                const unsigned out = unsigned(result) & 65535;
                const bool half = subtract ? int(a & 4095) - int(b & 4095) - c < 0 : (a & 4095) + (b & 4095) + c > 4095;
                unsigned flags = ((out >> 8) & 0x28) | (half ? 16 : 0) | (result < 0 || result > 65535 ? 1 : 0);
                if (!operation)
                    flags |= 0xc4;
                else
                {
                    const auto signedWord = [](unsigned n)
                    { return n < 32768 ? int(n) : int(n) - 65536; };
                    const int signedResult = signedWord(a) + (subtract ? -signedWord(b) - c : signedWord(b) + c);
                    flags |= ((out >> 8) & 0x80) | (out == 0 ? 64 : 0) | (subtract ? 2 : 0) |
                             (signedResult < -32768 || signedResult > 32767 ? 4 : 0);
                }
                check(((cpu.state().h << 8) | cpu.state().l) == int(out) && cpu.state().f == flags, "16-bit ALU result/flags");
            }
        auto bcd = [](unsigned n)
        { return ((n / 10) << 4) | (n % 10); };
        for (unsigned subtract = 0; subtract < 2; ++subtract)
            for (unsigned a = 0; a < 100; ++a)
                for (unsigned b = 0; b < 100; ++b)
                    for (unsigned carry = 0; carry < 2; ++carry)
                    {
                        Z80::State s;
                        s.a = static_cast<Byte>(bcd(a));
                        s.f = static_cast<Byte>(carry);
                        cpu.setState(s);
                        bus.memory[0] = subtract ? 0xde : 0xce;
                        bus.memory[1] = static_cast<Byte>(bcd(b));
                        bus.memory[2] = 0x27;
                        cpu.step();
                        check(cpu.step() == 4, "DAA timing");
                        const int decimal = subtract ? int(a) - int(b) - int(carry) : int(a) + int(b) + int(carry);
                        const unsigned packed = bcd(unsigned((decimal + 200) % 100));
                        check(cpu.state().a == packed && (cpu.state().f & 1) == unsigned(decimal < 0 || decimal > 99) &&
                                  bool(cpu.state().f & Z80::N) == bool(subtract) &&
                                  bool(cpu.state().f & Z80::PV) == evenParity(packed),
                              "Packed BCD arithmetic/DAA");
                    }
    }

    void branchesAndFlagHistory()
    {
        Bus bus;
        Z80 cpu(bus);
        const unsigned masks[] = {0x40, 0x40, 1, 1, 4, 4, 0x80, 0x80};
        for (unsigned condition = 0; condition < 8; ++condition)
            for (unsigned taken = 0; taken < 2; ++taken)
            {
                Z80::State s;
                s.sp = 0x9000;
                s.f = ((condition & 1) == taken) ? static_cast<Byte>(masks[condition]) : 0;
                cpu.setState(s);
                bus.memory[0] = static_cast<Byte>(0xc2 + 8 * condition);
                bus.memory[1] = 0x34;
                bus.memory[2] = 0x12;
                check(cpu.step() == 10 && cpu.state().pc == (taken ? 0x1234 : 3), "Conditional JP");
                cpu.setState(s);
                bus.memory[0] = static_cast<Byte>(0xc4 + 8 * condition);
                check(cpu.step() == (taken ? 17u : 10u) && cpu.state().pc == (taken ? 0x1234 : 3) &&
                          cpu.state().sp == (taken ? 0x8ffe : 0x9000),
                      "Conditional CALL");
                cpu.setState(s);
                bus.memory[0] = static_cast<Byte>(0xc0 + 8 * condition);
                bus.memory[0x9000] = 0x78;
                bus.memory[0x9001] = 0x56;
                check(cpu.step() == (taken ? 11u : 5u) && cpu.state().pc == (taken ? 0x5678 : 1), "Conditional RET");
            }
        Z80::State s;
        s.a = 0;
        s.f = s.q = 0x28;
        bus.memory[0] = 0x37;
        cpu.setState(s);
        cpu.step();
        check(cpu.state().f == 1, "SCF consumes previous flag-history latch");
        bus.memory[0] = 0xdd;
        bus.memory[1] = 0x37;
        cpu.setState(s);
        check(cpu.step() == 8 && cpu.state().f == 0x29, "Ignored prefix resets Q before SCF");
        s = {};
        s.pc = 0xfffe;
        s.b = 2;
        cpu.setState(s);
        bus.memory[0xfffe] = 0x10;
        bus.memory[0xffff] = 0xfe;
        check(cpu.step() == 13 && cpu.state().pc == 0xfffe && cpu.step() == 8 && cpu.state().pc == 0,
              "DJNZ taken/not-taken timing and PC wrapping");
    }

    void interrupts()
    {
        Bus bus;
        Z80 cpu(bus);
        bus.memory[0] = 0xfb;
        bus.memory[1] = 0x76;
        cpu.setIRQ(true);
        check(cpu.step() == 4 && cpu.state().eiDelay == 1, "EI delay begins");
        check(cpu.step() == 4 && cpu.state().halted, "EI permits following HALT");
        check(cpu.step() == 13 && cpu.state().pc == 0x38 && !cpu.state().halted && !cpu.state().iff1 && !cpu.state().iff2,
              "IM0 FF acknowledges IRQ and exits HALT");
        check(bus.acknowledgements == 1 && bus.memory[0xfffd] == 2, "Interrupt return address");

        Z80::State s;
        s.pc = 0x1234;
        s.sp = 0;
        s.iff1 = s.iff2 = true;
        s.im = 2;
        s.i = 0xff;
        cpu.setState(s);
        cpu.setIRQ(true);
        bus.memory[0xffff] = 0x78;
        bus.memory[0] = 0x56;
        // Evita sobrepor vetor e pilha, mantendo a leitura do vetor através de FFFF.
        s.sp = 0x8000;
        cpu.setState(s);
        cpu.setIRQ(true);
        check(cpu.step() == 19 && cpu.state().pc == 0x5678, "IM2 vector FF crosses FFFF");
        s.im = 1;
        s.irq = true;
        cpu.setState(s);
        check(cpu.step() == 13 && cpu.state().pc == 0x38, "IM1 fixed vector");
        s.im = 0;
        s.irq = true;
        bus.vector = 0xcd;
        bus.memory[0x1234] = 0x00;
        bus.memory[0x1235] = 0x90;
        cpu.setState(s);
        check(cpu.step() == 19 && cpu.state().pc == 0x9000 && bus.memory[0x7ffe] == 0x36, "IM0 injected CALL reads operands from PC");

        cpu.setState(s);
        bus.vector = 0xfb;
        bus.memory[0x1234] = 0;
        check(cpu.step() == 6 && cpu.state().eiDelay == 1, "IM0 injected EI includes acknowledge timing and one-instruction inhibition");
        check(cpu.step() == 4 && !cpu.state().eiDelay && cpu.state().pc == 0x1235, "IM0 EI permits one instruction before next IRQ");
        bus.vector = 0xff;
        check(cpu.step() == 13 && cpu.state().pc == 0x38, "IRQ following IM0 EI delay");

        cpu.setState(s);
        cpu.setNMI(true);
        check(cpu.step() == 11 && cpu.state().pc == 0x66 && cpu.state().iff2 && !cpu.state().iff1,
              "NMI has priority and retains IFF2");
        bus.memory[0x66] = 0xed;
        bus.memory[0x67] = 0x45;
        check(cpu.step() == 14 && cpu.state().pc == 0x1234 && cpu.state().iff1, "RETN restores IFF1");
        cpu.setIRQ(false);
        bus.memory[0x1234] = 0;
        check(cpu.step() == 4 && cpu.state().pc == 0x1235, "Held NMI is not retriggered");
        cpu.setNMI(false);
        cpu.setNMI(true);
        check(cpu.step() == 11, "New NMI edge triggers");
        bus.memory[0x67] = 0x4d;
        check(cpu.step() == 14 && bus.returns == 1, "RETI notifies daisy chain");

        s = {};
        cpu.setState(s);
        bus.memory[0] = 0xfb;
        bus.memory[1] = 0xf3;
        bus.memory[2] = 0x76;
        cpu.setIRQ(true);
        cpu.step();
        cpu.step();
        cpu.step();
        check(cpu.state().halted && !cpu.state().iff1 && cpu.step() == 4 && cpu.state().halted,
              "DI cancels EI delay; masked IRQ does not exit HALT");

        s = {};
        s.iff1 = s.iff2 = true;
        s.i = 0x80;
        cpu.setState(s);
        bus.memory[0] = 0xed;
        bus.memory[1] = 0x57;
        check(cpu.step() == 9 && (cpu.state().f & Z80::PV), "LD A,I copies IFF2");
        cpu.setIRQ(true);
        bus.vector = 0xff;
        cpu.step();
        check(!(cpu.state().f & Z80::PV), "NMOS interrupt clears LD A,I parity latch");
        cpu.reset();
        check(cpu.state().pc == 0 && cpu.state().sp == 0xffff && !cpu.state().irq && !cpu.state().cycles, "Reset");
        s = {};
        s.im = 3;
        bool rejected = false;
        try
        {
            cpu.setState(s);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid interrupt mode rejected");
    }

    void blocksAndIO()
    {
        Bus bus;
        Z80 cpu(bus);
        bus.memory[0] = 0xed;
        bus.memory[1] = 0xb0;
        bus.memory[0x1000] = 0x21;
        bus.memory[0x1001] = 0x80;
        bus.memory[0x1002] = 0xff;
        Z80::State s;
        s.h = 0x10;
        s.d = 0x20;
        s.c = 3;
        cpu.setState(s);
        check(cpu.step() == 21 && cpu.state().pc == 0 && cpu.state().c == 2, "LDIR repeats one iteration per step");
        check(cpu.step() == 21 && cpu.step() == 16 && cpu.state().pc == 2 && cpu.state().c == 0, "LDIR terminal timing");
        check(bus.memory[0x2000] == 0x21 && bus.memory[0x2001] == 0x80 && bus.memory[0x2002] == 0xff, "LDIR memory copy");
        bus.memory[1] = 0xb1;
        s.a = 0x80;
        s.c = 3;
        cpu.setState(s);
        check(cpu.step() == 21 && cpu.step() == 16 && cpu.state().c == 1 && (cpu.state().f & Z80::Z), "CPIR stops at matching byte");
        bus.memory[1] = 0xa2;
        s.b = 2;
        s.c = 0xff;
        bus.inputValue = 0x81;
        cpu.setState(s);
        check(cpu.step() == 16 && bus.inputs.back() == 0x02ff && bus.memory[0x1000] == 0x81 && cpu.state().b == 1,
              "INI uses original BC port");
        bus.memory[1] = 0xa3;
        cpu.setState(s);
        check(cpu.step() == 16 && bus.outputs.back() == std::make_pair(std::uint16_t(0x01ff), Byte(0x81)), "OUTI uses decremented B");
        bus.memory[0] = 0xdb;
        bus.memory[1] = 0x77;
        s.a = 0x45;
        s.f = 0xa5;
        cpu.setState(s);
        check(cpu.step() == 11 && bus.inputs.back() == 0x4577 && cpu.state().f == 0xa5, "IN A,(n) keeps flags and uses A as port high");
        bus.memory[0] = 0xd3;
        cpu.setState(s);
        check(cpu.step() == 11 && bus.outputs.back() == std::make_pair(std::uint16_t(0x4577), Byte(0x45)), "OUT (n),A port addressing");
    }

    void clockPhasesAndRestore()
    {
        using Pins = R2SEGA8::Core::Z80Pins;
        Bus bus;
        bus.tracing = true;
        Z80 cpu(bus);
        // LD (nn),A só pode modificar a RAM ao concluir o último ciclo de memória.
        bus.memory[0] = 0x32;
        bus.memory[1] = 0x00;
        bus.memory[2] = 0x40;
        Z80::State s;
        s.a = 0x5a;
        s.i = 0xab;
        s.r = 0x7f;
        cpu.setState(s);
        check(cpu.runCycles(2) == 2 && bus.readTimes.empty() && !cpu.instructionBoundary(), "M1 does not read early");
        check((cpu.pins().signals & (Pins::M1 | Pins::Memory | Pins::Read)) == (Pins::M1 | Pins::Memory | Pins::Read), "M1 read strobe on T2");
        cpu.tick();
        check(bus.readTimes == std::vector<unsigned>{3} && cpu.pins().address == 0xab7f && (cpu.pins().signals & Pins::Refresh), "M1 data and refresh on T3");
        cpu.runCycles(9);
        check(bus.readTimes == std::vector<unsigned>({3, 7, 10}) && bus.writeTimes.empty() &&
                  cpu.pins().address == 0x4000 && (cpu.pins().signals & Pins::Write),
              "Operand reads and write setup occur on distinct T-states");
        const auto saved = cpu.snapshot();
        const auto savedMemory = bus.memory;
        check(cpu.tick() && cpu.instructionBoundary() && bus.memory[0x4000] == 0x5a && bus.writeTimes == std::vector<unsigned>{13},
              "Memory write commits exactly on T13");
        const auto result = cpu.state();
        cpu.restore(saved);
        bus.memory = savedMemory;
        bus.writeTimes.clear();
        bus.ticks = 12;
        check(cpu.tick() && bus.writeTimes == std::vector<unsigned>{13} && cpu.state().pc == result.pc && cpu.state().cycles == result.cycles,
              "Mid-write snapshot resumes once without replaying bus effects");

        // Alterar o operando antes do T-state de leitura deve alterar o resultado.
        cpu.reset();
        bus.ticks = 0;
        bus.readTimes.clear();
        bus.writeTimes.clear();
        bus.trace.clear();
        bus.memory[0] = 0x3e;
        bus.memory[1] = 0x11;
        cpu.runCycles(6);
        bus.memory[1] = 0x88;
        check(cpu.tick() && cpu.state().a == 0x88 && cpu.state().cycles == 7, "Operand is sampled on its actual read T-state");

        // WAIT prolonga T2 sem repetir leituras; a liberação permite concluir o ciclo M1.
        cpu.reset();
        bus.memory[0] = 0;
        bus.ticks = 0;
        bus.readTimes.clear();
        cpu.tick();
        cpu.setWAIT(true);
        cpu.runCycles(5);
        check(bus.readTimes.empty() && !cpu.instructionBoundary() && cpu.state().cycles == 6, "WAIT stretches a transfer before its read");
        cpu.setWAIT(false);
        cpu.runCycles(3);
        check(cpu.instructionBoundary() && bus.readTimes == std::vector<unsigned>{8} && cpu.state().pc == 1, "WAIT releases without duplicated memory reads");

        // BUSRQ aguarda o fim do M1 e preserva a leitura imediata ainda pendente.
        cpu.reset();
        bus.ticks = 0;
        bus.readTimes.clear();
        bus.memory[0] = 0x3e;
        bus.memory[1] = 0x63;
        cpu.runCycles(2);
        cpu.setBusRequest(true);
        cpu.runCycles(2);
        check(bus.readTimes == std::vector<unsigned>{3}, "BUSRQ does not cut an active M1");
        cpu.runCycles(4);
        check((cpu.pins().signals & Pins::BusAck) && !cpu.pins().addressValid && bus.readTimes.size() == 1, "BUSACK suspends transfers at a machine-cycle boundary");
        const auto granted = cpu.snapshot();
        cpu.setBusRequest(false);
        cpu.runCycles(3);
        check(cpu.instructionBoundary() && cpu.state().a == 0x63 && cpu.state().cycles == 11, "Execution resumes after bus grant");
        cpu.restore(granted);
        cpu.setBusRequest(false);
        cpu.runCycles(3);
        check(cpu.instructionBoundary() && cpu.state().a == 0x63 && cpu.state().cycles == 11, "Snapshot retains a granted bus and pending operand");

        // INT recebido durante uma instrução só é reconhecido depois do seu último T-state.
        cpu.reset();
        s = {};
        s.iff1 = s.iff2 = true;
        s.im = 1;
        cpu.setState(s);
        bus.memory[0] = 0x3e;
        bus.memory[1] = 0x49;
        cpu.runCycles(3);
        cpu.setIRQ(true);
        cpu.runCycles(4);
        check(cpu.instructionBoundary() && cpu.state().a == 0x49 && !bus.acknowledgements, "IRQ waits for instruction boundary");
        check(cpu.step() == 13 && bus.acknowledgements == 1 && cpu.state().pc == 0x38, "IRQ acknowledge follows completed instruction");

        cpu.reset();
        s = {};
        s.iff1 = s.iff2 = true;
        s.im = 1;
        s.irq = true;
        s.busRequest = true;
        cpu.setState(s);
        const auto acknowledgements = bus.acknowledgements;
        cpu.runCycles(5);
        check(cpu.state().iff1 && cpu.state().pc == 0 && bus.acknowledgements == acknowledgements &&
                  (cpu.pins().signals & Pins::BusAck),
              "BUSACK delays a pending interrupt before any architectural change");
        cpu.setBusRequest(false);
        check(cpu.step() == 13 && cpu.state().pc == 0x38, "Pending interrupt enters after BUSACK release");

        // Os quatro T-states do I/O precisam transcorrer antes de consultar o dispositivo.
        cpu.reset();
        bus.ticks = 0;
        bus.inputTimes.clear();
        bus.memory[0] = 0xdb;
        bus.memory[1] = 0x22;
        cpu.runCycles(10);
        check(bus.inputTimes.empty(), "I/O is not sampled early");
        bus.inputValue = 0x91;
        check(cpu.tick() && cpu.state().a == 0x91 && bus.inputTimes == std::vector<unsigned>{11}, "I/O read occurs on T11");
    }

    void snapshotEveryPhase()
    {
        // Verifica a restauração em cada fase de um programa com indexação, bloco, pilha e I/O.
        Bus original;
        Z80 cpu(original);
        const Byte program[] = {0x31, 0x00, 0x90, 0xdd, 0x21, 0x00, 0x40, 0xdd, 0x36, 0xff, 0x81,
                                0xdd, 0xcb, 0xff, 0x00, 0x21, 0xff, 0x3f, 0x11, 0x00, 0x50,
                                0x01, 0x02, 0x00, 0xed, 0xb0, 0xc5, 0xd1, 0xdb, 0x22, 0xd3, 0x33, 0x76};
        std::copy(std::begin(program), std::end(program), original.memory.begin());
        for (unsigned phase = 0; phase < 180; ++phase)
        {
            cpu.tick();
            const auto saved = cpu.snapshot();
            Bus first, second;
            first.memory = second.memory = original.memory;
            Z80 a(first), b(second);
            a.restore(saved);
            b.restore(saved);
            a.runCycles(47);
            for (unsigned i = 0; i < 47; ++i)
                b.tick();
            check(first.memory == second.memory && first.writes == second.writes && first.inputs == second.inputs &&
                      first.outputs == second.outputs && a.state().pc == b.state().pc && a.state().f == b.state().f &&
                      a.state().cycles == b.state().cycles && a.state().r == b.state().r,
                  "Snapshot replay diverged by scheduling granularity");
            first.writes.clear();
            first.inputs.clear();
            first.outputs.clear();
            a.restore(saved);
            first.memory = original.memory;
            a.runCycles(47);
            check(first.memory == second.memory && first.writes == second.writes && first.inputs == second.inputs &&
                      first.outputs == second.outputs,
                  "Snapshot restore repeated or lost an external transfer");
        }
    }
}

int main()
{
    try
    {
        arithmetic();
        arithmetic16AndBCD();
        programsAndPrefixes();
        branchesAndFlagHistory();
        interrupts();
        blocksAndIO();
        clockPhasesAndRestore();
        snapshotEveryPhase();
        std::cout << "Z80 exhaustive 8-bit ALU, flags, programs, prefixes, stack, interrupts, block and I/O tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}