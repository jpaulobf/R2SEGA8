#pragma once

#include "Callbacks.h"
#include "FullscreenController.h"

namespace R2SEGA8::Window
{
    enum class DisplayShader
    {
        None,
        Gauss,
        HQ2,
        CRT,
        CRT3D,
        ScaleFX
    };
    enum class FullscreenMode
    {
        None,
        Stretched,
        Aspect
    };

    struct DisplayMenuContext
    {
        Callbacks &callbacks;
        FullscreenController &resolution;
        SDL_Window *window;
        unsigned &scale;
        unsigned &pendingScale;
        FullscreenMode &pendingFullscreen;
        DisplayShader &shader;
    };

    void drawDisplayMenu(DisplayMenuContext context);
}