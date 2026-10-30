#include "Core/CPU/Z80/Z80.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using R2SEGA8::Core::Z80;
    using R2SEGA8::Core::Z80Pins;

    // Falha com uma descrição do comportamento documentado, sem deixar a CPU em espera infinita.
    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Bus final : R2SEGA8::Core::Z80Bus
    {
        std::array<std::uint8_t, 65536> memory{};
        mutable std::vector<std::pair<unsigned, unsigned>> reads;
        std::vector<std::pair<unsigned, unsigned>> writes;
        std::vector<unsigned> portReads;
        std::vector<std::pair<unsigned, unsigned>> portWrites;
        std::vector<unsigned> returns;
        unsigned ticks = 0, acknowledgements = 0;
        std::uint8_t vector = 0xff;
        // Registra acessos físicos; o decodificador não pode duplicá-los ao retomar.
        std::uint8_t readMemory(std::uint16_t address) const override
        {
            reads.emplace_back(address, memory[address]);
            return memory[address];
        }
        // Registra a escrita antes de atualizar a memória emulada.
        void writeMemory(std::uint16_t address, std::uint8_t value) override
        {
            writes.emplace_back(address, value);
            memory[address] = value;
        }
        // Fornece um valor determinístico para as instruções de entrada em bloco.
        std::uint8_t input(std::uint16_t port) override
        {
            portReads.push_back(port);
            return 0x91;
        }
        // Aceita saídas sem acrescentar novos sinais à CPU.
        void output(std::uint16_t port, std::uint8_t value) override { portWrites.emplace_back(port, value); }
        // Registra reconhecimentos de INT para verificar sua prioridade relativa a NMI/BUSREQ.
        std::uint8_t interruptAcknowledge() override
        {
            ++acknowledgements;
            return vector;
        }
        // Marca o instante de decodificação do RETI no domínio de T-states já concluídos.
        void reti() override { returns.push_back(ticks); }
        // Mantém um relógio independente para verificar efeitos externos.
        void clock(const Z80Pins &) override { ++ticks; }
    };

    struct Program
    {
        const char *name;
        std::vector<std::uint8_t> code;
        std::vector<unsigned> machineCycles;
    };

    // Compara todos os registradores/latches arquiteturais, excluindo tempo e níveis dos sinais externos.
    auto architecture(const Z80::State &s)
    {
        return std::make_tuple(s.pc, s.sp, s.a, s.f, s.b, s.c, s.d, s.e, s.h, s.l, s.aAlt, s.fAlt, s.bAlt, s.cAlt, s.dAlt, s.eAlt, s.hAlt,
                               s.lAlt, s.ix, s.iy, s.i, s.r, s.im, s.q, s.iff1, s.iff2, s.halted, s.p, s.eiDelay, s.prefix);
    }

    // Prepara operandos de memória, pilha e registradores iguais para execução contínua e com DMA.
    void prepare(Bus &bus, Z80 &cpu, const Program &program)
    {
        std::copy(program.code.begin(), program.code.end(), bus.memory.begin());
        bus.memory[0x4000] = 0x91;
        bus.memory[0x4010] = 0x83;
        bus.memory[0x9000] = 0x34;
        bus.memory[0x9001] = 0x12;
        Z80::State state;
        state.a = 0x37;
        state.b = 2;
        state.c = 2;
        state.h = 0x40;
        state.d = 0x50;
        state.sp = 0x9000;
        state.ix = state.iy = 0x4000;
        cpu.setState(state);
    }

    // UM0080: BUSREQ deve ser atendido ao final de cada M-cycle, inclusive ciclos internos completos.
    void busRequestBoundaries()
    {
        const Program programs[] = {{"ADD HL,BC", {0x09}, {4, 4, 3}},
                                    {"ADC HL,BC", {0xed, 0x4a}, {4, 4, 4, 3}},
                                    {"SBC HL,BC", {0xed, 0x42}, {4, 4, 4, 3}},
                                    {"ADD IX,BC", {0xdd, 0x09}, {4, 4, 4, 3}},
                                    {"JR", {0x18, 0x02}, {4, 3, 5}},
                                    {"DJNZ", {0x10, 0x02}, {5, 3, 5}},
                                    {"RRD", {0xed, 0x67}, {4, 4, 3, 4, 3}},
                                    {"RLD", {0xed, 0x6f}, {4, 4, 3, 4, 3}},
                                    {"CPI", {0xed, 0xa1}, {4, 4, 3, 5}},
                                    {"CPD", {0xed, 0xa9}, {4, 4, 3, 5}},
                                    {"CPIR", {0xed, 0xb1}, {4, 4, 3, 5, 5}},
                                    {"LDIR", {0xed, 0xb0}, {4, 4, 3, 5, 5}},
                                    {"INIR", {0xed, 0xb2}, {4, 5, 4, 3, 5}},
                                    {"OTIR", {0xed, 0xb3}, {4, 5, 3, 4, 5}},
                                    {"LD A,(IX+d)", {0xdd, 0x7e, 0x10}, {4, 4, 3, 5, 3}},
                                    {"INC (IX+d)", {0xdd, 0x34, 0x10}, {4, 4, 3, 5, 4, 3}},
                                    {"LD (IX+d),n", {0xdd, 0x36, 0x10, 0x81}, {4, 4, 3, 5, 3}},
                                    {"RLC (IX+d)", {0xdd, 0xcb, 0x10, 0x06}, {4, 4, 3, 5, 4, 3}},
                                    {"BIT 0,(IX+d)", {0xdd, 0xcb, 0x10, 0x46}, {4, 4, 3, 5, 4}},
                                    {"INC BC", {0x03}, {6}},
                                    {"LD SP,HL", {0xf9}, {6}},
                                    {"PUSH BC", {0xc5}, {5, 3, 3}},
                                    {"CALL nn", {0xcd, 0x34, 0x12}, {4, 3, 4, 3, 3}},
                                    {"EX (SP),HL", {0xe3}, {4, 3, 4, 3, 5}},
                                    {"INC (HL)", {0x34}, {4, 4, 3}},
                                    {"RET NZ", {0xc0}, {5, 3, 3}}};
        unsigned checked = 0;
        for (const auto &program : programs)
        {
            Bus referenceBus;
            Z80 reference(referenceBus);
            prepare(referenceBus, reference, program);
            const unsigned total = std::accumulate(program.machineCycles.begin(), program.machineCycles.end(), 0u);
            check(reference.step() == total, std::string(program.name) + ": total de T-states");
            unsigned start = 0;
            for (const auto length : program.machineCycles)
            {
                const auto end = start + length;
                for (unsigned request = start + 2; request <= end; ++request)
                {
                    Bus bus;
                    Z80 cpu(bus);
                    prepare(bus, cpu, program);
                    cpu.runCycles(request - 1);
                    cpu.setBusRequest(true);
                    for (unsigned time = request; time <= end; ++time)
                    {
                        cpu.tick();
                        check(!(cpu.pins().signals & Z80Pins::BusAck), std::string(program.name) + ": BUSACK antecipado");
                    }
                    const auto accesses = bus.reads.size() + bus.writes.size();
                    cpu.runCycles(3);
                    check(cpu.pins().signals == Z80Pins::BusAck && !cpu.pins().addressValid && !cpu.pins().dataValid,
                          std::string(program.name) + ": BUSACK ausente apos M-cycle em T" + std::to_string(end));
                    check(bus.reads.size() + bus.writes.size() == accesses, std::string(program.name) + ": transferencia durante BUSACK");
                    cpu.setBusRequest(false);
                    cpu.runCycles(total - end);
                    check(cpu.instructionBoundary() && cpu.state().cycles == total + 3 && bus.memory == referenceBus.memory &&
                              bus.reads == referenceBus.reads && bus.writes == referenceBus.writes &&
                              architecture(cpu.state()) == architecture(reference.state()) && cpu.state().wz == reference.state().wz,
                          std::string(program.name) + ": retomada da instrucao apos BUSACK");
                    ++checked;
                }
                start = end;
            }
        }
        std::cout << checked << " BUSREQ phase cases passed\n";
    }

    // UM0081, figura 13: o periférico identifica ED/4D antes das leituras de retorno da pilha.
    void retiTiming()
    {
        Bus bus;
        Z80 cpu(bus);
        bus.memory[0] = 0xed;
        bus.memory[1] = 0x4d;
        bus.memory[0x9000] = 0x34;
        bus.memory[0x9001] = 0x12;
        Z80::State state;
        state.sp = 0x9000;
        state.iff2 = true;
        cpu.setState(state);
        cpu.runCycles(7);
        check(bus.returns.empty(), "RETI sinalizado antes da conclusao da busca M1");
        const auto before = cpu.snapshot();
        cpu.tick();
        check(bus.returns == std::vector<unsigned>{8} && bus.reads.size() == 2,
              "RETI deve notificar apos decodificar ED/4D, antes de ler a pilha");
        const auto after = cpu.snapshot();
        const auto memory = bus.memory;
        cpu.setBusRequest(true);
        cpu.runCycles(4);
        check(bus.returns.size() == 1 && bus.reads.size() == 2 && (cpu.pins().signals & Z80Pins::BusAck),
              "DMA apos ED/4D nao deve adiar ou repetir a notificacao RETI");
        cpu.setBusRequest(false);
        cpu.runCycles(6);
        check(cpu.state().pc == 0x1234 && cpu.state().iff1 && cpu.state().cycles == 18, "RETI deve concluir o retorno apos DMA");
        bus.ticks = 8;
        bus.memory = memory;
        cpu.restore(after);
        cpu.runCycles(6);
        check(bus.returns.size() == 1 && cpu.state().pc == 0x1234, "Snapshot apos notificacao nao pode repetir RETI");
        bus.ticks = 7;
        bus.returns.clear();
        cpu.restore(before);
        cpu.runCycles(7);
        check(bus.returns == std::vector<unsigned>{8}, "Snapshot anterior a notificacao deve emiti-la uma vez");
        for (const auto opcode : {0x45, 0x55, 0x5d, 0x65, 0x6d, 0x75, 0x7d})
        {
            bus.memory[1] = static_cast<std::uint8_t>(opcode);
            bus.returns.clear();
            cpu.setState(state);
            check(cpu.step() == 14 && bus.returns.empty(), "RETN e aliases nao podem sinalizar ED/4D ao periferico");
        }
    }

    // UM0080, modo 0: o opcode vem do dispositivo, substitui M1 e acrescenta dois T-states.
    void injectedInstructions()
    {
        for (unsigned opcode = 0; opcode < 256; ++opcode)
        {
            Bus normalBus, interruptBus;
            Z80 normal(normalBus), interrupted(interruptBus);
            Program program{"IM0", {static_cast<std::uint8_t>(opcode), 0x21, 0x34, 0x12, 0x00}, {}};
            prepare(normalBus, normal, program);
            prepare(interruptBus, interrupted, program);
            auto state = interrupted.state();
            state.pc = 1;
            state.iff1 = state.iff2 = true;
            state.irq = true;
            interrupted.setState(state);
            interruptBus.vector = static_cast<std::uint8_t>(opcode);
            const auto normalTime = normal.step();
            const auto injectedTime = interrupted.step();
            check(injectedTime == normalTime + 2 && architecture(interrupted.state()) == architecture(normal.state()) &&
                      interrupted.state().wz == normal.state().wz && interruptBus.memory == normalBus.memory &&
                      interruptBus.writes == normalBus.writes && interruptBus.portReads == normalBus.portReads &&
                      interruptBus.portWrites == normalBus.portWrites,
                  "IM0 opcode " + std::to_string(opcode) + ": divergencia semantica ou de temporizacao");
        }
    }

    // Verifica prioridade de BUSREQ, retenção da borda NMI e estado IFF após atendimento aninhado.
    void interruptSequences()
    {
        Bus bus;
        Z80 cpu(bus);
        Z80::State state;
        state.iff1 = state.iff2 = true;
        state.im = 1;
        state.pc = 0x1234;
        state.sp = 0x9000;
        state.busRequest = true;
        state.irq = true;
        cpu.setState(state);
        cpu.setNMI(true);
        cpu.setNMI(false);
        cpu.runCycles(8);
        const auto saved = cpu.snapshot();
        check(cpu.state().pc == 0x1234 && cpu.state().nmiPending && !bus.acknowledgements && (cpu.pins().signals & Z80Pins::BusAck),
              "BUSREQ deve adiar NMI e INT, preservando a borda NMI");
        cpu.setBusRequest(false);
        check(cpu.step() == 11 && cpu.state().pc == 0x66 && !cpu.state().iff1 && cpu.state().iff2 && !bus.acknowledgements,
              "NMI deve vencer INT apos a liberacao de BUSACK");
        cpu.setNMI(true);
        check(cpu.step() == 11 && cpu.state().iff2, "NMI aninhada preserva IFF2 conforme a tabela 1 do manual");
        bus.memory[0x66] = 0xed;
        bus.memory[0x67] = 0x45;
        cpu.setIRQ(false);
        check(cpu.step() == 14 && cpu.state().pc == 0x66 && cpu.state().iff1, "RETN restaura IFF1 sem alterar IFF2");
        check(cpu.step() == 14 && cpu.state().pc == 0x1234, "RETN externo recupera o endereco original");
        cpu.restore(saved);
        check(cpu.state().nmiPending && cpu.state().busRequest, "Snapshot deve restaurar a borda NMI durante BUSACK");
        cpu.setBusRequest(false);
        check(cpu.step() == 11 && cpu.state().pc == 0x66, "Snapshot de BUSACK deve retomar a NMI pendente");

        cpu.reset();
        bus.memory[0] = 0xfb;
        bus.memory[1] = 0xfb;
        bus.memory[2] = 0xf3;
        bus.memory[3] = 0;
        cpu.setIRQ(true);
        const auto acknowledged = bus.acknowledgements;
        cpu.runCycles(16);
        check(cpu.state().pc == 4 && !cpu.state().iff1 && bus.acknowledgements == acknowledged,
              "EI consecutivos devem adiar INT; DI deve cancelar a habilitacao");
    }

    // A cessão de execução após 256 prefixos não termina a instrução e não autoriza setState.
    void prefixStateBoundary()
    {
        Bus bus;
        Z80 cpu(bus);
        std::fill_n(bus.memory.begin(), 256, 0xdd);
        check(cpu.step() == 1024 && !cpu.instructionBoundary(), "Lote de prefixos nao pode criar fronteira de instrucao");
        const auto saved = cpu.snapshot();
        bool rejected = false;
        try
        {
            cpu.setState({});
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        check(rejected, "setState deve rejeitar uma instrucao ainda pendente apos o lote de prefixos");
        cpu.restore(saved);
        check(cpu.step() == 4 && cpu.state().pc == 257 && cpu.instructionBoundary(),
              "Snapshot deve poder restaurar a continuacao de prefixos sem passar pela restricao de setState");
    }

    // WAIT deve adiar a transferência, sem duplicar efeitos ao liberar a linha ou restaurar a fase pendente.
    void waitAndSnapshots()
    {
        struct WaitCase
        {
            const char *name;
            std::vector<std::uint8_t> code;
            unsigned waitAt, total;
            bool irq = false, nmi = false;
        };
        const WaitCase cases[] = {{"Memory write", {0x32, 0x00, 0x40}, 12, 13},
                                  {"OUT", {0xd3, 0x40}, 10, 11},
                                  {"IN", {0xdb, 0x40}, 10, 11},
                                  {"IM1", {0x00}, 4, 13, true, false},
                                  {"NMI", {0x00}, 2, 11, false, true}};
        for (const auto &test : cases)
            for (unsigned hold : {1u, 3u, 7u, 17u})
            {
                Bus referenceBus, bus;
                Z80 reference(referenceBus), cpu(bus);
                Program program{test.name, test.code, {}};
                prepare(referenceBus, reference, program);
                prepare(bus, cpu, program);
                auto state = cpu.state();
                state.im = 1;
                state.iff1 = state.iff2 = true;
                state.irq = test.irq;
                state.nmiLine = state.nmiPending = test.nmi;
                reference.setState(state);
                cpu.setState(state);
                check(reference.step() == test.total, std::string(test.name) + ": referencia sem WAIT");
                cpu.runCycles(test.waitAt - 1);
                const auto transfers =
                    bus.reads.size() + bus.writes.size() + bus.portReads.size() + bus.portWrites.size() + bus.acknowledgements;
                cpu.setWAIT(true);
                cpu.runCycles(hold);
                check(bus.reads.size() + bus.writes.size() + bus.portReads.size() + bus.portWrites.size() + bus.acknowledgements ==
                          transfers,
                      std::string(test.name) + ": WAIT nao pode repetir ou antecipar a transferencia");
                const auto saved = cpu.snapshot();
                Bus restoredBus = bus;
                Z80 restored(restoredBus);
                restored.restore(saved);
                for (Z80 *execution : {&cpu, &restored})
                {
                    execution->setWAIT(false);
                    execution->runCycles(test.total - test.waitAt + 1);
                    check(execution->instructionBoundary() && execution->state().cycles == test.total + hold &&
                              architecture(execution->state()) == architecture(reference.state()) &&
                              execution->state().wz == reference.state().wz,
                          std::string(test.name) + ": WAIT/snapshot alterou o resultado ou a duracao");
                }
                for (const Bus *result : {&bus, &restoredBus})
                    check(result->memory == referenceBus.memory && result->reads == referenceBus.reads &&
                              result->writes == referenceBus.writes && result->portReads == referenceBus.portReads &&
                              result->portWrites == referenceBus.portWrites && result->acknowledgements == referenceBus.acknowledgements,
                          std::string(test.name) + ": transferencia perdida ou duplicada depois de WAIT/snapshot");
            }
    }
}

int main(int argc, char **argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "all";
        check(group == "all" || group == "busreq" || group == "reti" || group == "control" || group == "wait",
              "Grupo de auditoria desconhecido");
        if (group == "all" || group == "busreq")
            busRequestBoundaries();
        if (group == "all" || group == "reti")
            retiTiming();
        if (group == "all" || group == "control")
        {
            injectedInstructions();
            interruptSequences();
            prefixStateBoundary();
        }
        if (group == "all" || group == "wait")
            waitAndSnapshots();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}