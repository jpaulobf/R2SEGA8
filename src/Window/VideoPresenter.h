#pragma once

#include "DisplayMenu.h"
#include "Window.h"
#include <memory>
#include <vector>

namespace R2SEGA8::Window
{
    class NtscShaderPipeline;

    // Possui as texturas e os buffers usados para apresentar o quadro da máquina.
    class VideoPresenter
    {
    public:
        VideoPresenter();
        ~VideoPresenter();
        void release();
        void draw(FrameView frame, SDL_Window *window, SDL_Renderer *renderer, DisplaySize nativeSize,
                  unsigned visibleWidth, FullscreenMode fullscreen, DisplayShader &shader,
                  Callbacks &callbacks, bool pointerAvailable);

    private:
        SDL_Texture *videoTexture = nullptr, *shaderTexture = nullptr;
        DisplayShader initializedShader = DisplayShader::None;
        unsigned shaderSourceWidth = 0;
        int shaderWidth = 0, shaderHeight = 0;
        unsigned videoWidth = 0, videoHeight = 0;
        std::unique_ptr<NtscShaderPipeline> shaderPipeline;
        std::vector<std::uint32_t> shaderInput, shaderOutput;
        std::vector<std::uint8_t> scanlinePixels;
    };
}