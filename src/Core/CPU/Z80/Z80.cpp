#include "Z80.h"
#include <stdexcept>
#include <utility>

namespace R2SEGA8::Core
{
    namespace
    {
        using Byte = std::uint8_t;
        using Word = std::uint16_t;
        constexpr Word word(Byte high, Byte low) { return static_cast<Word>((high << 8) | low); }
        constexpr Byte high(Word value) { return static_cast<Byte>(value >> 8); }
        constexpr Byte low(Word value) { return static_cast<Byte>(value); }
        constexpr bool parity(Byte value)
        {
            value ^= value >> 4;
            value ^= value >> 2;
            value ^= value >> 1;
            return (value & 1) == 0;
        }
        constexpr Byte sz53(Byte value) { return (value & 0xa8) | (value == 0 ? Z80::Z : 0); }
        constexpr Byte sz53p(Byte value) { return sz53(value) | (parity(value) ? Z80::PV : 0); }
    }

    void Z80::reset()
    {
        // Registradores sem valor definido de reset usam zero por convenção determinística.
        s_ = State{};
        initial_ = s_;
        pins_ = {};
        decoderPins_ = {};
        cycle_ = {};
        active_ = flagsChanged_ = false;
        completedCount_ = cursor_ = 0;
        irqLine_ = nmiLevel_ = nmiPending_ = waitLine_ = busRequestLine_ = false;
    }

    void Z80::setState(const State &state)
    {
        if (!instructionBoundary())
            throw std::logic_error("Restore Z80 state only at an instruction boundary.");
        installState(state);
    }

    void Z80::installState(const State &state)
    {
        if (state.im > 2 || state.eiDelay > 2 || (state.prefix != 0 && state.prefix != 0xdd && state.prefix != 0xfd))
            throw std::invalid_argument("Invalid Z80 control state.");
        s_ = state;
        irqLine_ = s_.irq;
        nmiLevel_ = s_.nmiLine;
        nmiPending_ = s_.nmiPending;
        waitLine_ = s_.wait;
        busRequestLine_ = s_.busRequest;
        flagsChanged_ = false;
    }

    void Z80::setIRQ(bool asserted) { s_.irq = irqLine_ = asserted; }

    void Z80::setWAIT(bool asserted) { s_.wait = waitLine_ = asserted; }

    void Z80::setBusRequest(bool asserted) { s_.busRequest = busRequestLine_ = asserted; }

    void Z80::setNMI(bool asserted)
    {
        if (asserted && !nmiLevel_)
            s_.nmiPending = nmiPending_ = true;
        s_.nmiLine = nmiLevel_ = asserted;
    }

    void Z80::refresh() { s_.r = (s_.r & 0x80) | ((s_.r + 1) & 0x7f); }

    Byte Z80::fetch()
    {
        const auto value = access(CycleKind::Fetch, s_.pc, 4);
        ++s_.pc;
        refresh();
        return value;
    }

    Byte Z80::immediate()
    {
        const auto value = read(s_.pc);
        ++s_.pc;
        return value;
    }

    Word Z80::immediate16()
    {
        const auto l = immediate();
        return word(immediate(), l);
    }

    Word Z80::read16(Word address)
    {
        const auto l = read(address);
        return word(read(static_cast<Word>(address + 1)), l);
    }

    void Z80::write16(Word address, Word value)
    {
        write(address, low(value));
        write(static_cast<Word>(address + 1), high(value));
    }

    void Z80::push(Word value)
    {
        write(--s_.sp, high(value));
        write(--s_.sp, low(value));
    }

    Word Z80::pop()
    {
        const auto l = read(s_.sp);
        ++s_.sp;
        const auto h = read(s_.sp);
        ++s_.sp;
        return word(h, l);
    }

    Word Z80::hl() const { return word(s_.h, s_.l); }

    void Z80::setHL(Word value)
    {
        s_.h = high(value);
        s_.l = low(value);
    }

    Word Z80::pair(unsigned code, unsigned index) const
    {
        switch (code)
        {
        case 0:
            return word(s_.b, s_.c);
        case 1:
            return word(s_.d, s_.e);
        case 2:
            return index == 1 ? s_.ix : index == 2 ? s_.iy
                                                   : hl();
        default:
            return s_.sp;
        }
    }

    void Z80::setPair(unsigned code, Word value, unsigned index)
    {
        switch (code)
        {
        case 0:
            s_.b = high(value);
            s_.c = low(value);
            break;
        case 1:
            s_.d = high(value);
            s_.e = low(value);
            break;
        case 2:
            if (index == 1)
                s_.ix = value;
            else if (index == 2)
                s_.iy = value;
            else
                setHL(value);
            break;
        default:
            s_.sp = value;
            break;
        }
    }

    Byte Z80::reg(unsigned code, unsigned index, Word address)
    {
        switch (code)
        {
        case 0:
            return s_.b;
        case 1:
            return s_.c;
        case 2:
            return s_.d;
        case 3:
            return s_.e;
        case 4:
            return high(pair(2, index));
        case 5:
            return low(pair(2, index));
        case 6:
            return read(address);
        default:
            return s_.a;
        }
    }

    void Z80::setReg(unsigned code, unsigned index, Word address, Byte value)
    {
        switch (code)
        {
        case 0:
            s_.b = value;
            break;
        case 1:
            s_.c = value;
            break;
        case 2:
            s_.d = value;
            break;
        case 3:
            s_.e = value;
            break;
        case 4:
            setPair(2, word(value, low(pair(2, index))), index);
            break;
        case 5:
            setPair(2, word(high(pair(2, index)), value), index);
            break;
        case 6:
            write(address, value);
            break;
        default:
            s_.a = value;
            break;
        }
    }

    void Z80::flags(Byte value)
    {
        s_.f = value;
        flagsChanged_ = true;
    }

    bool Z80::condition(unsigned code) const
    {
        const Byte masks[] = {Z, Z, C, C, PV, PV, S, S};
        return ((s_.f & masks[code]) != 0) == ((code & 1) != 0);
    }

    Byte Z80::incDec(Byte value, bool decrement)
    {
        const Byte result = static_cast<Byte>(value + (decrement ? -1 : 1));
        flags((s_.f & C) | sz53(result) | (decrement ? N : 0) |
              (((value ^ result) & 0x10) ? H : 0) |
              (value == (decrement ? 0x80 : 0x7f) ? PV : 0));
        return result;
    }

    void Z80::alu(unsigned operation, Byte value)
    {
        const auto a = s_.a;
        const unsigned carry = ((operation == 1 || operation == 3) && (s_.f & C)) ? 1 : 0;
        if (operation < 4 || operation == 7)
        {
            const bool sub = operation == 2 || operation == 3 || operation == 7;
            const unsigned result = sub ? unsigned(a) - value - carry : unsigned(a) + value + carry;
            const auto out = static_cast<Byte>(result);
            flags(sz53(out) | (sub ? N : 0) | ((result & 0x100) ? C : 0) |
                  (((a ^ value ^ out) & 0x10) ? H : 0) |
                  (((sub ? (a ^ value) : ~(a ^ value)) & (a ^ out) & 0x80) ? PV : 0));
            if (operation == 7)
                flags((s_.f & ~(X | Y)) | (value & (X | Y)));
            else
                s_.a = out;
        }
        else
        {
            s_.a = operation == 4 ? (a & value) : operation == 5 ? (a ^ value)
                                                                 : (a | value);
            flags(sz53p(s_.a) | (operation == 4 ? H : 0));
        }
    }

    void Z80::add16(Word value, unsigned index)
    {
        const auto left = pair(2, index);
        const unsigned result = unsigned(left) + value;
        s_.wz = static_cast<Word>(left + 1);
        setPair(2, static_cast<Word>(result), index);
        flags((s_.f & (S | Z | PV)) | (high(static_cast<Word>(result)) & (X | Y)) |
              (((left ^ value ^ result) & 0x1000) ? H : 0) | (result > 0xffff ? C : 0));
    }

    void Z80::adcSbc16(Word value, bool subtract)
    {
        const auto left = hl();
        const unsigned carry = s_.f & C;
        const unsigned result = subtract ? unsigned(left) - value - carry : unsigned(left) + value + carry;
        const auto out = static_cast<Word>(result);
        s_.wz = static_cast<Word>(left + 1);
        setHL(out);
        flags((high(out) & (S | X | Y)) | (out == 0 ? Z : 0) | (subtract ? N : 0) |
              ((result & 0x10000) ? C : 0) | (((left ^ value ^ out) & 0x1000) ? H : 0) |
              (((subtract ? (left ^ value) : ~(left ^ value)) & (left ^ out) & 0x8000) ? PV : 0));
    }

    unsigned Z80::cb(Byte opcode, unsigned index, Word address)
    {
        const unsigned x = opcode >> 6, y = (opcode >> 3) & 7, z = opcode & 7;
        const auto value = reg(index ? 6 : z, 0, address);
        if (index || z == 6)
            idle(1);
        Byte result = value;
        if (x == 0)
        {
            Byte carry = 0;
            switch (y)
            {
            case 0:
                carry = value >> 7;
                result = (value << 1) | carry;
                break;
            case 1:
                carry = value & 1;
                result = (value >> 1) | (carry << 7);
                break;
            case 2:
                carry = value >> 7;
                result = (value << 1) | (s_.f & C);
                break;
            case 3:
                carry = value & 1;
                result = (value >> 1) | ((s_.f & C) << 7);
                break;
            case 4:
                carry = value >> 7;
                result = value << 1;
                break;
            case 5:
                carry = value & 1;
                result = (value >> 1) | (value & 0x80);
                break;
            case 6:
                carry = value >> 7;
                result = (value << 1) | 1;
                break; // NMOS SLL
            case 7:
                carry = value & 1;
                result = value >> 1;
                break;
            }
            flags(sz53p(result) | carry);
        }
        else if (x == 1)
        {
            const bool zero = (value & (1 << y)) == 0;
            // BIT (HL) usa o byte alto anterior de MEMPTR; BIT indexado usa o endereço efetivo.
            const auto xy = (index || z == 6) ? high(s_.wz) : value;
            flags((s_.f & C) | H | (xy & (X | Y)) | (zero ? Z | PV : 0) |
                  (y == 7 && !zero ? S : 0));
        }
        else if (x == 2)
            result = value & ~(1 << y);
        else
            result = value | (1 << y);
        if (x != 1)
        {
            if (index)
                write(address, result);
            if (!index || z != 6)
                setReg(z, 0, address, result);
        }
        return index ? (x == 1 ? 16 : 19) : z == 6 ? (x == 1 ? 12 : 15)
                                                   : 8;
    }

    unsigned Z80::base(Byte opcode, unsigned index)
    {
        const unsigned x = opcode >> 6, y = (opcode >> 3) & 7, z = opcode & 7, p = y >> 1, q = y & 1;
        Word address = hl();
        const bool memory = (x == 0 && (z == 4 || z == 5 || z == 6) && y == 6) ||
                            (x == 1 && opcode != 0x76 && (y == 6 || z == 6)) || (x == 2 && z == 6);
        if (index && memory)
        {
            const auto displacement = static_cast<std::int8_t>(immediate());
            address = static_cast<Word>(pair(2, index) + displacement);
            s_.wz = address;
            if (opcode != 0x36)
                internalCycle(5);
        }
        if (x == 0)
        {
            switch (z)
            {
            case 0:
                if (y == 0)
                    return 4;
                if (y == 1)
                {
                    std::swap(s_.a, s_.aAlt);
                    std::swap(s_.f, s_.fAlt);
                    return 4;
                }
                {
                    if (y == 2)
                        idle(1);
                    const auto displacement = static_cast<std::int8_t>(immediate());
                    const bool take = y == 2 ? --s_.b != 0 : y == 3 || condition(y - 4);
                    if (take)
                        internalCycle(5);
                    if (take)
                        s_.wz = s_.pc = static_cast<Word>(s_.pc + displacement);
                    return y == 2 ? (take ? 13 : 8) : take ? 12
                                                           : 7;
                }
            case 1:
                if (!q)
                {
                    setPair(p, immediate16(), index);
                    return 10;
                }
                // UM0080: ADD de 16 bits usa M-cycles internos de quatro e três T-states.
                internalCycle(4);
                internalCycle(3);
                add16(pair(p, index), index);
                return 11;
            case 2:
                if (p < 2)
                {
                    const auto location = pair(p);
                    if (!q)
                    {
                        write(location, s_.a);
                        s_.wz = word(s_.a, low(static_cast<Word>(location + 1)));
                    }
                    else
                    {
                        s_.a = read(location);
                        s_.wz = static_cast<Word>(location + 1);
                    }
                    return 7;
                }
                else
                {
                    const auto location = immediate16();
                    s_.wz = static_cast<Word>(location + 1);
                    if (p == 2)
                    {
                        if (q)
                            setPair(2, read16(location), index);
                        else
                            write16(location, pair(2, index));
                        return 16;
                    }
                    if (q)
                        s_.a = read(location);
                    else
                    {
                        write(location, s_.a);
                        s_.wz = word(s_.a, low(s_.wz));
                    }
                    return 13;
                }
            case 3:
                idle(2);
                setPair(p, static_cast<Word>(pair(p, index) + (q ? -1 : 1)), index);
                return 6;
            case 4:
            case 5:
            {
                const auto operand = reg(y, index, address);
                if (y == 6)
                    idle(1);
                const auto value = incDec(operand, z == 5);
                setReg(y, index, address, value);
                return y == 6 ? (index ? 19 : 11) : 4;
            }
            case 6:
            {
                const auto value = immediate();
                if (y == 6 && index)
                    idle(2);
                setReg(y, index, address, value);
                return y == 6 ? (index ? 15 : 10) : 7;
            }
            case 7:
            {
                const auto a = s_.a;
                const auto oldCarry = s_.f & C;
                Byte carry = 0;
                if (y < 4)
                {
                    if (y == 0 || y == 2)
                    {
                        carry = a >> 7;
                        s_.a = (a << 1) | (y == 0 ? carry : oldCarry);
                    }
                    else
                    {
                        carry = a & 1;
                        s_.a = (a >> 1) | ((y == 1 ? carry : oldCarry) << 7);
                    }
                    flags((s_.f & (S | Z | PV)) | (s_.a & (X | Y)) | carry);
                }
                else if (y == 4)
                {
                    Byte correction = 0;
                    const bool newCarry = oldCarry || a > 0x99;
                    if ((s_.f & H) || (a & 15) > 9)
                        correction |= 6;
                    if (newCarry)
                        correction |= 0x60;
                    s_.a = static_cast<Byte>(a + ((s_.f & N) ? -correction : correction));
                    flags((s_.f & N) | sz53p(s_.a) | (((a ^ s_.a) & 0x10) ? H : 0) | (newCarry ? C : 0));
                }
                else if (y == 5)
                {
                    s_.a ^= 0xff;
                    flags((s_.f & (S | Z | PV | C)) | H | N | (s_.a & (X | Y)));
                }
                else
                {
                    const Byte xy = (a | (s_.f ^ s_.q)) & (X | Y);
                    flags((s_.f & (S | Z | PV)) | xy | (y == 6 ? C : oldCarry ? H
                                                                              : C));
                }
                return 4;
            }
            }
        }
        if (x == 1)
        {
            if (opcode == 0x76)
            {
                s_.halted = true;
                return 4;
            }
            // Nas cargas com memória indexada, H/L são os registradores reais, sem substituição por IXH/IXL.
            const unsigned registerIndex = memory ? 0 : index;
            setReg(y, registerIndex, address, reg(z, registerIndex, address));
            return memory ? (index ? 15 : 7) : 4;
        }
        if (x == 2)
        {
            alu(y, reg(z, index, address));
            return z == 6 ? (index ? 15 : 7) : 4;
        }
        switch (z)
        {
        case 0:
            idle(1);
            if (!condition(y))
                return 5;
            s_.wz = s_.pc = pop();
            return 11;
        case 1:
            if (!q)
            {
                const auto value = pop();
                if (p == 3)
                {
                    s_.a = high(value);
                    s_.f = low(value);
                }
                else
                    setPair(p, value, index);
                return 10;
            }
            switch (p)
            {
            case 0:
                s_.wz = s_.pc = pop();
                return 10;
            case 1:
                std::swap(s_.b, s_.bAlt);
                std::swap(s_.c, s_.cAlt);
                std::swap(s_.d, s_.dAlt);
                std::swap(s_.e, s_.eAlt);
                std::swap(s_.h, s_.hAlt);
                std::swap(s_.l, s_.lAlt);
                return 4;
            case 2:
                s_.pc = pair(2, index);
                return 4;
            default:
                idle(2);
                s_.sp = pair(2, index);
                return 6;
            }
        case 2:
        {
            const auto target = immediate16();
            s_.wz = target;
            if (condition(y))
                s_.pc = target;
            return 10;
        }
        case 3:
            switch (y)
            {
            case 0:
                s_.wz = s_.pc = immediate16();
                return 10;
            case 2:
            {
                const auto port = word(s_.a, immediate());
                access(CycleKind::Output, port, 4, s_.a);
                s_.wz = word(s_.a, low(static_cast<Word>(port + 1)));
                return 11;
            }
            case 3:
            {
                const auto port = word(s_.a, immediate());
                s_.a = access(CycleKind::Input, port, 4);
                s_.wz = static_cast<Word>(port + 1);
                return 11;
            }
            case 4:
            {
                const auto value = read16(s_.sp);
                idle(1);
                const auto old = pair(2, index);
                write(static_cast<Word>(s_.sp + 1), high(old));
                write(s_.sp, low(old));
                idle(2);
                setPair(2, value, index);
                s_.wz = value;
                return 19;
            }
            case 5:
            {
                const auto value = pair(1);
                setPair(1, hl());
                setHL(value);
                return 4;
            }
            case 6:
                s_.iff1 = s_.iff2 = false;
                s_.eiDelay = 0;
                return 4;
            case 7:
                s_.iff1 = s_.iff2 = true;
                s_.eiDelay = 2;
                return 4;
            default:
                throw std::logic_error("Z80 prefix reached base decoder.");
            }
        case 4:
        {
            const auto target = immediate16();
            s_.wz = target;
            if (!condition(y))
                return 10;
            idle(1);
            push(s_.pc);
            s_.pc = target;
            return 17;
        }
        case 5:
            if (!q)
            {
                idle(1);
                push(p == 3 ? word(s_.a, s_.f) : pair(p, index));
                return 11;
            }
            if (p == 0)
            {
                const auto target = immediate16();
                idle(1);
                push(s_.pc);
                s_.wz = s_.pc = target;
                return 17;
            }
            throw std::logic_error("Z80 prefix reached base decoder.");
        case 6:
            alu(y, immediate());
            return 7;
        case 7:
            idle(1);
            push(s_.pc);
            s_.wz = s_.pc = static_cast<Word>(y * 8);
            return 11;
        }
        throw std::logic_error("Invalid Z80 decode.");
    }

    unsigned Z80::ed(Byte opcode)
    {
        const unsigned x = opcode >> 6, y = (opcode >> 3) & 7, z = opcode & 7, p = y >> 1, q = y & 1;
        if (x == 1)
        {
            switch (z)
            {
            case 0:
            {
                const auto port = pair(0);
                const auto value = access(CycleKind::Input, port, 4);
                if (y != 6)
                    setReg(y, 0, 0, value);
                flags((s_.f & C) | sz53p(value));
                s_.wz = static_cast<Word>(port + 1);
                return 12;
            }
            case 1:
            {
                const auto port = pair(0);
                access(CycleKind::Output, port, 4, y == 6 ? 0 : reg(y, 0, 0));
                s_.wz = static_cast<Word>(port + 1);
                return 12;
            }
            case 2:
                internalCycle(4);
                internalCycle(3);
                adcSbc16(pair(p), !q);
                return 15;
            case 3:
            {
                const auto address = immediate16();
                if (q)
                    setPair(p, read16(address));
                else
                    write16(address, pair(p));
                s_.wz = static_cast<Word>(address + 1);
                return 20;
            }
            case 4:
            {
                const auto value = s_.a;
                s_.a = 0;
                alu(2, value);
                return 8;
            }
            case 5:
                // O periférico decodifica ED/4D após a busca M1, antes das leituras da pilha.
                if (opcode == 0x4d)
                    access(CycleKind::Reti, 0, 0);
                s_.wz = s_.pc = pop();
                s_.iff1 = s_.iff2;
                return 14;
            case 6:
            {
                constexpr Byte modes[] = {0, 0, 1, 2, 0, 0, 1, 2};
                s_.im = modes[y];
                return 8;
            }
            case 7:
                switch (y)
                {
                case 0:
                    idle(1);
                    s_.i = s_.a;
                    return 9;
                case 1:
                    idle(1);
                    s_.r = s_.a;
                    return 9;
                case 2:
                case 3:
                    idle(1);
                    s_.a = y == 2 ? s_.i : s_.r;
                    flags((s_.f & C) | sz53(s_.a) | (s_.iff2 ? PV : 0));
                    s_.p = true;
                    return 9;
                case 4:
                case 5:
                {
                    const auto value = read(hl());
                    internalCycle(4);
                    Byte result;
                    if (y == 4)
                    {
                        result = (s_.a << 4) | (value >> 4);
                        s_.a = (s_.a & 0xf0) | (value & 15);
                    }
                    else
                    {
                        result = (value << 4) | (s_.a & 15);
                        s_.a = (s_.a & 0xf0) | (value >> 4);
                    }
                    write(hl(), result);
                    s_.wz = static_cast<Word>(hl() + 1);
                    flags((s_.f & C) | sz53p(s_.a));
                    return 18;
                }
                default:
                    return 8;
                }
            }
        }
        if (x == 2 && y >= 4 && z <= 3)
        {
            const int direction = (y & 1) ? -1 : 1;
            const bool repeat = y >= 6;
            bool again = false;
            if (z == 0) // LDI/LDD/LDIR/LDDR
            {
                const auto value = read(hl());
                write(pair(1), value);
                idle(2);
                setHL(static_cast<Word>(hl() + direction));
                setPair(1, static_cast<Word>(pair(1) + direction));
                setPair(0, static_cast<Word>(pair(0) - 1));
                const Byte sum = s_.a + value;
                flags((s_.f & (S | Z | C)) | (pair(0) ? PV : 0) | (sum & X) | ((sum & 2) << 4));
                again = pair(0) != 0;
            }
            else if (z == 1) // CPI/CPD/CPIR/CPDR
            {
                const auto value = read(hl());
                internalCycle(5);
                const Byte result = s_.a - value;
                const bool half = ((s_.a ^ value ^ result) & 16) != 0;
                const Byte adjusted = result - (half ? 1 : 0);
                setHL(static_cast<Word>(hl() + direction));
                setPair(0, static_cast<Word>(pair(0) - 1));
                flags((s_.f & C) | N | (sz53(result) & (S | Z)) | (half ? H : 0) |
                      (pair(0) ? PV : 0) | (adjusted & X) | ((adjusted & 2) << 4));
                s_.wz = static_cast<Word>(s_.wz + direction);
                again = pair(0) && result;
            }
            else // INI/IND/INIR/INDR/OUTI/OUTD/OTIR/OTDR
            {
                idle(1);
                Byte value;
                unsigned sum;
                if (z == 2)
                {
                    const auto port = pair(0);
                    value = access(CycleKind::Input, port, 4);
                    write(hl(), value);
                    --s_.b;
                    s_.wz = static_cast<Word>(port + direction);
                    sum = unsigned(value) + static_cast<Byte>(s_.c + direction);
                    setHL(static_cast<Word>(hl() + direction));
                }
                else
                {
                    value = read(hl());
                    --s_.b;
                    access(CycleKind::Output, pair(0), 4, value);
                    setHL(static_cast<Word>(hl() + direction));
                    s_.wz = static_cast<Word>(pair(0) + direction);
                    sum = unsigned(value) + s_.l;
                }
                flags(sz53(s_.b) | (value & 0x80 ? N : 0) | (sum > 255 ? H | C : 0) |
                      (parity(static_cast<Byte>((sum & 7) ^ s_.b)) ? PV : 0));
                again = s_.b != 0;
            }
            if (repeat && again)
            {
                internalCycle(5);
                s_.pc = static_cast<Word>(s_.pc - 2);
                s_.wz = static_cast<Word>(s_.pc + 1);
                flags((s_.f & ~(X | Y)) | (high(s_.pc) & (X | Y)));
                if (z >= 2)
                {
                    // O ciclo adicional da repetição altera H/PV, além de X/Y derivados de PC.
                    const bool carry = (s_.f & C) != 0, negative = (s_.f & N) != 0;
                    const int adjustment = carry ? (negative ? -1 : 1) : 0;
                    Byte result = s_.f;
                    if (!parity(static_cast<Byte>((s_.b + adjustment) & 7)))
                        result ^= PV;
                    if (carry)
                        result = (result & ~H) | ((s_.b & 15) == (negative ? 0 : 15) ? H : 0);
                    flags(result);
                }
                return 21;
            }
            return 16;
        }
        return 8; // Codificações ED indefinidas funcionam como NOPs de dois bytes no Z80 NMOS.
    }

    unsigned Z80::execute(Byte opcode, unsigned prefixCycles)
    {
        unsigned index = s_.prefix == 0xdd ? 1 : s_.prefix == 0xfd ? 2
                                                                   : 0;
        s_.prefix = 0;
        unsigned prefixes = 0;
        while (opcode == 0xdd || opcode == 0xfd)
        {
            s_.q = 0; // Um prefixo ignorado ou indexado limpa o histórico de flags.
            index = opcode == 0xdd ? 1 : 2;
            prefixCycles += 4;
            if (++prefixes == 256)
            {
                s_.prefix = opcode;
                return prefixCycles;
            }
            opcode = fetch();
        }
        if (opcode == 0xcb)
        {
            if (index)
            {
                const auto displacement = static_cast<std::int8_t>(immediate());
                s_.wz = static_cast<Word>(pair(2, index) + displacement);
                const auto operation = immediate();
                idle(2);
                return prefixCycles + cb(operation, index, s_.wz);
            }
            return prefixCycles + cb(fetch(), 0, hl());
        }
        if (opcode == 0xed)
            return prefixCycles + ed(fetch());
        return prefixCycles + base(opcode, index);
    }
}