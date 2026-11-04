#pragma once
#include "Callbacks.h"
#include <memory>
union SDL_Event;
struct SDL_Window;
namespace R2SEGA8::Window
{
    class DebugWindow
    {
    public:
        // Associa a janela de depuração aos callbacks e à janela principal.
        DebugWindow(Callbacks &callbacks, SDL_Window *parent);
        
        // Libera os recursos da janela de depuração.
        ~DebugWindow();
        
        // Abre a janela de depuração se ainda estiver fechada.
        void open();
        
        // Fecha a janela de depuração se estiver aberta.
        void close();
        
        // Informa se a janela de depuração está aberta.
        bool isOpen() const;
        
        // Processa eventos da janela de depuração e indica quando foram consumidos.
        bool processEvent(const SDL_Event &event);
        
        // Desenha o estado atual do depurador quando a janela está visível.
        void render();
        
        // Posiciona a janela de depuração ao lado da janela principal.
        void snapToParent();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}