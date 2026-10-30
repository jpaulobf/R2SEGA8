#pragma once
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace R2SEGA8::Systems
{
    // Agenda quadros pela razão ciclos/clock, preservando a fração de nanossegundo entre prazos.
    class FramePacer
    {
    public:
        using Clock = std::chrono::steady_clock;

        // Inicializa o ritmo da interface sem máquina em 60 Hz; reset define o perfil do console.
        FramePacer() { reset(60, 1, Clock::now()); }

        // Inicia uma nova linha de tempo para uma razão válida, sem carregar atrasos de outra ROM.
        void reset(unsigned frequency, unsigned cycles, Clock::time_point now)
        {
            if (!frequency || !cycles)
                throw std::invalid_argument("Invalid frame timing.");
            frequency_ = frequency;
            cycles_ = cycles;
            remainder_ = 0;
            deadline_ = now;
        }

        // Avança exatamente um período racional, sem arredondar FPS ou acumular atrasos de sleep.
        void advance()
        {
            remainder_ += static_cast<std::uint64_t>(cycles_) * 1000000000ull;
            deadline_ += std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(remainder_ / frequency_));
            remainder_ %= frequency_;
        }

        // Consulta o próximo prazo sem depender de eventos ou do dispositivo de áudio.
        Clock::time_point deadline() const { return deadline_; }

    private:
        unsigned frequency_ = 60, cycles_ = 1;
        std::uint64_t remainder_ = 0;
        Clock::time_point deadline_;
    };
}