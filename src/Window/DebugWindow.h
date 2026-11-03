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
        DebugWindow(Callbacks &callbacks, SDL_Window *parent);
        ~DebugWindow();
        void open();
        void close();
        bool isOpen() const;
        bool processEvent(const SDL_Event &event);
        void render();
        void snapToParent();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}