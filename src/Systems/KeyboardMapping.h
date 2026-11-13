#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace R2Keyboard
{
    // Primeiro índice: controle; segundo: função exibida no diálogo. Zero indica ausência de tecla.
    using Mapping = std::array<std::array<SDL_Keycode, 10>, 2>;
    // Retorna as associações de fábrica usadas na inicialização e na opção Default.
    inline Mapping defaults()
    {
        return {{{SDLK_W, SDLK_S, SDLK_A, SDLK_D, SDLK_J, SDLK_K, SDLK_U, SDLK_I, SDLK_RETURN, SDLK_BACKSPACE},
                 {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_Z, SDLK_X, SDLK_C, SDLK_V, SDLK_1, SDLK_2}}};
    }
    // Exclui teclas sem nome e as reservadas aos atalhos do emulador.
    inline bool assignable(SDL_Keycode key)
    {
        if (!key || key == SDLK_TAB || key == SDLK_CAPSLOCK || key == SDLK_PAUSE || key == SDLK_P ||
            (key >= SDLK_F5 && key <= SDLK_F12) || key == SDLK_ESCAPE ||
            key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT || key == SDLK_RALT ||
            key == SDLK_LGUI || key == SDLK_RGUI)
            return false;
        return SDL_GetKeyName(key)[0] != '\0';
    }
}