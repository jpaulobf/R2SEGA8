#include "Z80.h"
#include <stdexcept>
#include <string>

namespace R2SEGA8::Core
{
    namespace
    {
        using Byte = std::uint8_t;
        using Word = std::uint16_t;
        // Combina os bytes sem depender da ordem nativa do processador hospedeiro.
        constexpr Word word(Byte high, Byte low) { return static_cast<Word>((high << 8) | low); }
    }

    unsigned Z80::instruction()
    {
        unsigned duration;
        flagsChanged_ = false;
        if (!s_.prefix && s_.nmiPending)
        {
            s_.nmiPending = false;
            s_.halted = false;
            s_.iff1 = false;
            access(CycleKind::NMI, s_.pc, 5);
            refresh();
            push(s_.pc);
            s_.wz = s_.pc = 0x66;
            s_.q = 0;
            s_.p = false;
            duration = 11;
        }
        else if (!s_.prefix && s_.irq && s_.iff1 && !s_.eiDelay)
        {
            s_.halted = false;
            s_.iff1 = s_.iff2 = false;
            if (s_.p)
                s_.f &= ~PV;
            s_.p = false;
            const auto vector = access(CycleKind::Interrupt, s_.pc, s_.im == 0 ? 6 : 7);
            refresh();
            if (s_.im == 0)
            {
                duration = execute(vector) + 2;
                if (!s_.prefix && s_.eiDelay)
                    --s_.eiDelay;
            }
            else
            {
                push(s_.pc);
                s_.wz = s_.pc = s_.im == 1 ? 0x38 : read16(word(s_.i, vector));
                duration = s_.im == 1 ? 13 : 19;
            }
            s_.q = flagsChanged_ ? s_.f : 0;
        }
        else if (s_.halted)
        {
            access(CycleKind::Fetch, s_.pc, 4);
            refresh();
            s_.q = 0;
            s_.p = false;
            duration = 4;
        }
        else
        {
            s_.p = false;
            duration = execute(fetch());
            if (!s_.prefix)
            {
                s_.q = flagsChanged_ ? s_.f : 0;
                if (s_.eiDelay)
                    --s_.eiDelay;
            }
        }
        return duration;
    }

    Byte Z80::access(CycleKind kind, Word address, unsigned length, Byte value)
    {
        if (!length && kind != CycleKind::Reti)
            return 0;
        if (cursor_ < completedCount_)
        {
            const auto &done = completed_[cursor_++];
            if (done.kind != kind || done.address != address || done.length != length || done.data != value)
                throw std::logic_error("Z80 micro-operation replay diverged.");
            decoderPins_.address = kind == CycleKind::Fetch || kind == CycleKind::NMI || kind == CycleKind::Interrupt
                                       ? done.refreshAddress
                                       : address;
            return done.result;
        }
        cycle_ = {kind, address, word(s_.i, s_.r), value, 0, 0, length};
        // Retorna ao sequenciador sem criar uma exceção para cada transferência.
        // IMPORTANTE: entre este ponto e resume(), os temporários do decodificador devem
        // permanecer triviais, sem RAII/destrutores. Nenhum periférico é consultado aqui.
        std::longjmp(yieldPoint_, 1);
    }

    void Z80::idle(unsigned length) { access(CycleKind::Internal, decoderPins_.address, length); }
    Byte Z80::read(Word address) { return access(CycleKind::Read, address, 3); }
    void Z80::write(Word address, Byte value) { access(CycleKind::Write, address, 3, value); }

    void Z80::resume()
    {
        const auto elapsed = s_.cycles;
        for (;;)
        {
            s_ = initial_;
            cursor_ = 0;
            decoderPins_ = {};
            if (setjmp(yieldPoint_) == 0)
            {
                const auto expected = instruction();
                unsigned actual = 0;
                for (unsigned i = 0; i < completedCount_; ++i)
                    actual += completed_[i].length;
                if (actual != expected)
                    throw std::logic_error("Z80 internal timing mismatch: " + std::to_string(actual) + " / " + std::to_string(expected));
                active_ = false;
            }
            else
            {
                if (cycle_.kind == CycleKind::Reti)
                {
                    bus_.reti();
                    completed_[completedCount_++] = cycle_;
                    continue;
                }
            }
            break;
        }
        // Os níveis recebidos durante a instrução pertencem à linha do tempo externa.
        s_.cycles = elapsed;
        s_.irq = irqLine_;
        s_.nmiLine = nmiLevel_;
        s_.nmiPending = nmiPending_;
        s_.wait = waitLine_;
        s_.busRequest = busRequestLine_;
    }

    bool Z80::tick()
    {
        // Durante BUSACK, nenhuma instrução ou interrupção nova pode começar.
        if (!active_ && busRequestLine_)
        {
            pins_ = {};
            pins_.signals = Z80Pins::BusAck;
            pins_.addressValid = false;
            ++s_.cycles;
            bus_.clock(pins_);
            return false;
        }
        if (!active_)
        {
            initial_ = s_;
            completedCount_ = 0;
            active_ = true;
            if (!s_.prefix && s_.nmiPending)
                nmiPending_ = false;
            resume();
        }
        pins_ = {};
        pins_.address = cycle_.address;
        const auto kind = cycle_.kind;
        const auto phase = cycle_.phase;
        if (s_.busRequest && phase == 0 && kind != CycleKind::Internal)
        {
            pins_.signals = Z80Pins::BusAck;
            pins_.addressValid = false;
            ++s_.cycles;
            bus_.clock(pins_);
            return false;
        }
        switch (kind)
        {
        case CycleKind::Fetch:
        case CycleKind::NMI:
            if (phase < 2)
                pins_.signals = Z80Pins::M1;
            if (phase == 1)
                pins_.signals |= Z80Pins::Memory | Z80Pins::Read;
            if (phase >= 2)
            {
                pins_.address = cycle_.refreshAddress;
                if (phase < 4)
                    pins_.signals = Z80Pins::Refresh | Z80Pins::Memory;
                if (phase == 2)
                {
                    cycle_.result = bus_.readMemory(cycle_.address);
                    pins_.data = cycle_.result;
                    pins_.dataValid = true;
                }
            }
            break;
        case CycleKind::Read:
            if (phase == 1)
                pins_.signals = Z80Pins::Memory | Z80Pins::Read;
            if (phase == 2)
            {
                cycle_.result = bus_.readMemory(cycle_.address);
                pins_.data = cycle_.result;
                pins_.dataValid = true;
            }
            break;
        case CycleKind::Write:
            if (phase == 1)
            {
                pins_.signals = Z80Pins::Memory | Z80Pins::Write;
                pins_.data = cycle_.data;
                pins_.dataValid = true;
            }
            if (phase == 2)
                bus_.writeMemory(cycle_.address, cycle_.data);
            break;
        case CycleKind::Input:
        case CycleKind::Output:
            if (phase == 2)
            {
                pins_.signals = Z80Pins::IO | (kind == CycleKind::Input ? Z80Pins::Read : Z80Pins::Write);
                if (kind == CycleKind::Output)
                {
                    pins_.data = cycle_.data;
                    pins_.dataValid = true;
                }
            }
            if (phase == 3)
            {
                if (kind == CycleKind::Input)
                {
                    cycle_.result = bus_.input(cycle_.address);
                    pins_.data = cycle_.result;
                    pins_.dataValid = true;
                }
                else
                    bus_.output(cycle_.address, cycle_.data);
            }
            break;
        case CycleKind::Interrupt:
            if (phase < 4)
                pins_.signals = Z80Pins::M1;
            if (phase == 2 || phase == 3)
                pins_.signals |= Z80Pins::IO;
            if (phase >= 4)
            {
                pins_.address = cycle_.refreshAddress;
                if (phase < 6)
                    pins_.signals = Z80Pins::Refresh | Z80Pins::Memory;
                if (phase == 4)
                {
                    cycle_.result = bus_.interruptAcknowledge();
                    pins_.data = cycle_.result;
                    pins_.dataValid = true;
                }
            }
            break;
        case CycleKind::Internal:
        case CycleKind::Reti:
            break;
        }
        ++s_.cycles;
        bus_.clock(pins_);
        const unsigned waitPhase = kind == CycleKind::Input || kind == CycleKind::Output ? 2 : kind == CycleKind::Interrupt ? 3
                                                                                                                            : 1;
        const bool waitable = kind != CycleKind::Internal && kind != CycleKind::Reti;
        if (waitable && phase == waitPhase && waitLine_)
            return false;
        if (++cycle_.phase == cycle_.length)
        {
            if (completedCount_ == completed_.size())
                throw std::logic_error("Z80 micro-operation capacity exceeded.");
            completed_[completedCount_++] = cycle_;
            resume();
        }
        return !active_;
    }

    unsigned Z80::step()
    {
        const auto start = s_.cycles;
        while (!tick())
        {
        }
        return static_cast<unsigned>(s_.cycles - start);
    }

    std::uint64_t Z80::runCycles(std::uint64_t budget)
    {
        for (std::uint64_t i = 0; i < budget; ++i)
            tick();
        return budget;
    }

    Z80::Snapshot Z80::snapshot() const
    {
        Snapshot result;
        result.state = s_;
        result.initial = initial_;
        result.pins = pins_;
        result.completed = completed_;
        result.cycle = cycle_;
        result.completedCount = completedCount_;
        result.active = active_;
        return result;
    }

    void Z80::restore(const Snapshot &saved)
    {
        // O snapshot é opaco: todos os campos foram produzidos pela própria CPU.
        active_ = false;
        setState(saved.state);
        initial_ = saved.initial;
        pins_ = saved.pins;
        completed_ = saved.completed;
        cycle_ = saved.cycle;
        completedCount_ = saved.completedCount;
        active_ = saved.active;
    }
}