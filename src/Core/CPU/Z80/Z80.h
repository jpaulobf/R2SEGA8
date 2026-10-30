#pragma once
#include "Core/Bus/Z80Bus.h"
#include <array>
#include <csetjmp>
#include <cstdint>

namespace R2SEGA8::Core
{
    // Z80 NMOS: cada tick representa exatamente um T-state da máquina.
    class Z80
    {
    public:
        enum Flag : std::uint8_t
        {
            C = 1,
            N = 2,
            PV = 4,
            X = 8,
            H = 16,
            Y = 32,
            Z = 64,
            S = 128
        };
        struct State
        {
            std::uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
            std::uint8_t aAlt = 0, fAlt = 0, bAlt = 0, cAlt = 0, dAlt = 0, eAlt = 0, hAlt = 0, lAlt = 0;
            std::uint16_t ix = 0, iy = 0, sp = 0xffff, pc = 0, wz = 0;
            std::uint8_t i = 0, r = 0, im = 0, q = 0;
            bool iff1 = false, iff2 = false, halted = false;
            bool irq = false, nmiLine = false, nmiPending = false, p = false;
            bool wait = false, busRequest = false;
            std::uint8_t eiDelay = 0, prefix = 0;
            std::uint64_t cycles = 0;
        };
        // Conecta a CPU ao barramento e aplica o estado inicial determinístico.
        explicit Z80(Z80Bus &bus) : bus_(bus) { reset(); }

        // Impede copiar referências e uma execução pendente.
        Z80(const Z80 &) = delete;

        // Impede substituir referências e a continuação da CPU por atribuição.
        Z80 &operator=(const Z80 &) = delete;

        // Reinicia registradores e sequenciador, sem apagar a memória do sistema.
        void reset();

        // Expõe registradores atuais; uma instrução pode estar parcialmente executada.
        const State &state() const { return s_; }

        // Restaura registradores somente entre instruções; valida os campos de controle.
        void setState(const State &state);

        // Define o nível da interrupção mascarável, amostrada entre instruções.
        void setIRQ(bool asserted);

        // Define NMI e retém sua borda de ativação até o reconhecimento.
        void setNMI(bool asserted);

        // Mantém a transferência atual em espera enquanto WAIT estiver ativo.
        void setWAIT(bool asserted);

        // Solicita cessão do barramento ao final do ciclo de máquina atual.
        void setBusRequest(bool asserted);

        // Executa um T-state; sinaliza conclusão de instrução, interrupção, HALT ou lote de prefixos.
        bool tick();

        // Retorna os sinais e valores observáveis no T-state mais recente.
        const Z80Pins &pins() const { return pins_; }

        // Informa se uma instrução completa terminou e não há continuação de prefixos.
        bool instructionBoundary() const { return !active_ && !s_.prefix; }

        // Avança até concluir a instrução atual; 256 prefixos consecutivos cedem o controle.
        unsigned step();

        // Executa exatamente o orçamento de T-states, preservando uma instrução incompleta.
        std::uint64_t runCycles(std::uint64_t budget);

    private:
        enum class CycleKind
        {
            Fetch,
            Read,
            Write,
            Input,
            Output,
            Internal,
            InternalCycle,
            Interrupt,
            NMI,
            Reti
        };
        struct Cycle
        {
            CycleKind kind = CycleKind::Internal;
            std::uint16_t address = 0, refreshAddress = 0;
            std::uint8_t data = 0, result = 0;
            unsigned phase = 0, length = 0;
        };
        Z80Bus &bus_;
        State s_, initial_;
        Z80Pins pins_, decoderPins_;

        // Transferências concluídas: a retomada usa seus resultados sem repetir efeitos externos.
        std::array<Cycle, 272> completed_{};
        Cycle cycle_;
        unsigned completedCount_ = 0, cursor_ = 0;
        bool active_ = false, flagsChanged_ = false;
        bool irqLine_ = false, nmiLevel_ = false, nmiPending_ = false, waitLine_ = false, busRequestLine_ = false;
        std::jmp_buf yieldPoint_;

        // Valida e instala registradores e linhas externas; restore também usa este caminho para fases incompletas.
        void installState(const State &state);

        // Retoma cálculos puros até a próxima micro-operação ou o fim da instrução.
        void resume();

        // Executa a lógica da instrução com resultados de transferências já concluídas.
        unsigned instruction();

        // Agenda uma micro-operação; somente tick executa efeitos externos no barramento.
        std::uint8_t access(CycleKind kind, std::uint16_t address, unsigned length, std::uint8_t value = 0);

        // Acrescenta T-states internos ao M-cycle atual, mantendo o último endereço do barramento.
        void idle(unsigned length);

        // Inicia um M-cycle interno independente, permitindo BUSACK antes dele e ignorando WAIT.
        void internalCycle(unsigned length);

        // Agenda um ciclo de leitura de memória de três T-states.
        std::uint8_t read(std::uint16_t address);

        // Agenda um ciclo de escrita de memória de três T-states.
        void write(std::uint16_t address, std::uint8_t value);

        // Busca opcode em M1, avançando PC e os sete bits inferiores de R.
        std::uint8_t fetch();

        // Lê um operando imediato e avança PC sem incrementar R.
        std::uint8_t immediate();

        // Lê um operando imediato de 16 bits em ordem little endian.
        std::uint16_t immediate16();

        // Lê uma palavra, incluindo a volta de FFFF para 0000.
        std::uint16_t read16(std::uint16_t address);

        // Escreve uma palavra em ordem little endian.
        void write16(std::uint16_t address, std::uint16_t value);

        // Empilha primeiro o byte alto, decrementando SP antes de cada escrita.
        void push(std::uint16_t value);

        // Desempilha uma palavra e incrementa SP após cada leitura.
        std::uint16_t pop();

        // Incrementa a porção de atualização de R sem modificar o bit 7.
        void refresh();

        // Combina H e L em um endereço de 16 bits.
        std::uint16_t hl() const;

        // Divide um valor entre os registradores H e L.
        void setHL(std::uint16_t value);

        // Seleciona BC, DE, HL/IX/IY ou SP conforme o opcode.
        std::uint16_t pair(unsigned code, unsigned index = 0) const;

        // Atualiza o par selecionado pela codificação do opcode.
        void setPair(unsigned code, std::uint16_t value, unsigned index = 0);

        // Obtém um registrador de oito bits ou agenda a leitura de memória.
        std::uint8_t reg(unsigned code, unsigned index, std::uint16_t address);

        // Atualiza um registrador de oito bits ou agenda a escrita de memória.
        void setReg(unsigned code, unsigned index, std::uint16_t address, std::uint8_t value);

        // Atualiza F e registra a alteração para o latch interno Q.
        void flags(std::uint8_t value);

        // Avalia uma das oito condições de salto do Z80.
        bool condition(unsigned code) const;

        // Calcula INC/DEC de oito bits, preservando carry.
        std::uint8_t incDec(std::uint8_t value, bool decrement);

        // Executa a ULA de oito bits, incluindo CP e flags não documentadas.
        void alu(unsigned operation, std::uint8_t value);

        // Soma um par a HL/IX/IY preservando S, Z e P/V.
        void add16(std::uint16_t value, unsigned index);

        // Calcula ADC/SBC HL com flags de 16 bits.
        void adcSbc16(std::uint16_t value, bool subtract);

        // Decodifica instruções básicas e substituições de registradores DD/FD.
        unsigned base(std::uint8_t opcode, unsigned index);

        // Decodifica rotações, deslocamentos, BIT, RES e SET, incluindo DDCB/FDCB.
        unsigned cb(std::uint8_t opcode, unsigned index, std::uint16_t address);

        // Decodifica instruções estendidas ED e operações de bloco.
        unsigned ed(std::uint8_t opcode);

        // Resolve prefixos e encaminha o opcode ao grupo apropriado.
        unsigned execute(std::uint8_t opcode, unsigned prefixCycles = 0);

    public:
        // Guarda também a fase e os dados intermediários; memória e periféricos pertencem ao sistema.
        struct Snapshot
        {
        private:
            friend class Z80;
            State state, initial;
            Z80Pins pins;
            std::array<Cycle, 272> completed{};
            Cycle cycle;
            unsigned completedCount = 0;
            bool active = false;
        };

        // Captura a CPU em qualquer T-state, sem executar acessos adicionais ao barramento.
        Snapshot snapshot() const;

        // Restaura a fase exata; o chamador restaura a memória e os periféricos correspondentes.
        void restore(const Snapshot &snapshot);
    };
}