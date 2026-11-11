#pragma once

namespace R2SEGA8::Systems
{
    // Mantém independentes as causas de pausa da sessão.
    class PauseState
    {
    public:
        // Alterna somente a intenção manual, inclusive quando um menu já suspende a execução.
        void toggleManual() { manual_ = !manual_; }

        // Reflete abertura/fechamento de menus e diálogos sem modificar a intenção manual.
        void setMenuPaused(bool paused) { menu_ = paused; }

        void setMinimized(bool paused) { minimized_ = paused; }
        void setDebugPaused(bool paused) { debug_ = paused; }

        // A execução só pode continuar quando nenhuma origem pede pausa.
        bool paused() const { return manual_ || menu_ || minimized_ || debug_; }

    private:
        bool manual_ = false, menu_ = false, minimized_ = false, debug_ = false;
    };
}