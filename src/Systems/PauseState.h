#pragma once

namespace R2SEGA8::Systems
{
    // Mantém independentes a pausa manual e a pausa temporária da interface.
    class PauseState
    {
    public:
        // Alterna somente a intenção manual, inclusive quando um menu já suspende a execução.
        void toggleManual() { manual_ = !manual_; }

        // Reflete abertura/fechamento de menus e diálogos sem modificar a intenção manual.
        void setMenuPaused(bool paused) { menu_ = paused; }

        // A execução só pode continuar quando nenhuma das duas origens pede pausa.
        bool paused() const { return manual_ || menu_; }

    private:
        bool manual_ = false, menu_ = false;
    };
}