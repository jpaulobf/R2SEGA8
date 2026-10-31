#pragma once
#include <chrono>
#include <cstdint>

namespace R2SEGA8::Systems
{
    // Mede quadros emulados por segundo real, independentemente de quantas imagens são apresentadas.
    class FrameCounter
    {
    public:
        using Clock = std::chrono::steady_clock;

        // Inicia uma janela de medição sem depender de SDL ou do relógio emulado.
        explicit FrameCounter(Clock::time_point now = Clock::now()) { reset(now); }

        // Descarta a medição anterior somente ao carregar, reiniciar ou descarregar a máquina.
        void reset(Clock::time_point now)
        {
            start_ = now;
            frames_ = 0;
            fps_ = 0;
        }

        // Retira o tempo sem emulação da janela atual, preservando contagem e média ao retomar uma pausa.
        void excludePause(Clock::duration duration)
        {
            if (duration > Clock::duration::zero())
                start_ += duration;
        }

        // Atualiza a média a cada quadro com a contagem atual; renova a janela sem apagar o último valor.
        void record(Clock::time_point now)
        {
            ++frames_;
            const auto elapsed = now - start_;
            if (elapsed <= Clock::duration::zero())
                return;
            fps_ = frames_ / std::chrono::duration<double>(elapsed).count();
            if (elapsed >= std::chrono::milliseconds(500))
            {
                frames_ = 0;
                start_ = now;
            }
        }

        // Entrega a média parcial mais recente, preservada entre quadros e mudanças de ritmo.
        double fps() const { return fps_; }

    private:
        Clock::time_point start_;
        std::uint64_t frames_ = 0;
        double fps_ = 0;
    };
}