#pragma once
#include <cstdint>

namespace R2SEGA8::Core
{
    struct Z80Pins
    {
        enum Signal : unsigned
        {
            Memory = 1,
            IO = 2,
            Read = 4,
            Write = 8,
            M1 = 16,
            Refresh = 32,
            BusAck = 64
        };
        std::uint16_t address = 0;
        std::uint8_t data = 0;
        unsigned signals = 0;
        bool addressValid = true, dataValid = false;
    };

    // Barramento de memória e portas de 16 bits, com amostragem por T-state.
    // Os strobes representam transferências; não simulam bordas elétricas de meio clock.
    class Z80Bus
    {
    public:
        // Permite destruir a implementação concreta pelo contrato do barramento.
        virtual ~Z80Bus() = default;

        // Fornece o dado no T-state de conclusão da leitura de memória.
        virtual std::uint8_t readMemory(std::uint16_t address) const = 0;

        // Efetiva a escrita no T-state de conclusão do ciclo de memória.
        virtual void writeMemory(std::uint16_t address, std::uint8_t value) = 0;

        // Fornece o dado na conclusão do ciclo de entrada da porta indicada.
        virtual std::uint8_t input(std::uint16_t port) = 0;

        // Efetiva a saída na conclusão do ciclo da porta indicada.
        virtual void output(std::uint16_t port, std::uint8_t value) = 0;

        // Fornece o opcode/vetor durante o reconhecimento de INT; FF representa o padrão sem dispositivo.
        virtual std::uint8_t interruptAcknowledge() { return 0xff; }

        // Notifica a decodificação de ED/4D após M1 e antes da pilha, para cadeias de prioridade de interrupção.
        virtual void reti() {}

        // Avança os periféricos por um T-state, inclusive em HALT, WAIT e cessão do barramento.
        virtual void clock(const Z80Pins &) {}
    };
}