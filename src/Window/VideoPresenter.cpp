#include "VideoPresenter.h"
#include "VideoEffects.h"
#include "VideoLayout.h"
#include "NtscShaderPipeline.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace R2SEGA8::Window
{
    struct PixelArtDraw
    {
        SDL_Renderer *renderer;
        SDL_Texture *texture;
        SDL_FRect destination;
    };
    // O backend ImGui sobrescreve o filtro das imagens; desenha Pixel Art diretamente pela SDL.
    static void drawPixelArt(const ImDrawList *, const ImDrawCmd *command)
    {
        const auto &draw = *static_cast<const PixelArtDraw *>(command->UserCallbackData);
        SDL_Rect clip{};
        const bool clipped = SDL_RenderClipEnabled(draw.renderer);
        if ((clipped && !SDL_GetRenderClipRect(draw.renderer, &clip)) ||
            !SDL_SetRenderClipRect(draw.renderer, nullptr) ||
            !SDL_SetTextureScaleMode(draw.texture, SDL_SCALEMODE_PIXELART) ||
            !SDL_RenderTexture(draw.renderer, draw.texture, nullptr, &draw.destination) ||
            !SDL_SetRenderClipRect(draw.renderer, clipped ? &clip : nullptr))
            throw std::runtime_error(SDL_GetError());
    }

    VideoPresenter::VideoPresenter() = default;
    VideoPresenter::~VideoPresenter() { release(); }
    void VideoPresenter::release()
    {
        if (videoTexture)
            SDL_DestroyTexture(videoTexture);
        if (shaderTexture)
            SDL_DestroyTexture(shaderTexture);
        videoTexture = shaderTexture = nullptr;
        shaderPipeline.reset();
    }

    void VideoPresenter::draw(FrameView frame, SDL_Window *window, SDL_Renderer *renderer, DisplaySize nativeSize,
                              unsigned visibleWidth, FullscreenMode fullscreen, DisplayShader &shader,
                              Callbacks &callbacks, bool pointerAvailable)
    {
        if (!frame.rgba || !frame.width || !frame.height)
            return;
        const auto limit = static_cast<unsigned>(std::numeric_limits<int>::max());
        if (frame.width > limit / 4 || frame.height > limit || frame.pitch > limit || frame.pitch < frame.width * 4)
            throw std::invalid_argument("Invalid presentation frame dimensions or pitch.");
        const auto settings = callbacks.executionSettings ? callbacks.executionSettings() : Common::ExecutionSettings{};
        const unsigned crop = settings.cropLeftEnabled && frame.width > 8 && nativeSize.width > 8 ? 8 : 0;
        const unsigned width = frame.width - crop;
        const bool pixelArt = settings.pixelArtEnabled;
        const bool bilinear = settings.bilinearFilterEnabled && !pixelArt;
        if (!videoTexture || videoWidth != width || videoHeight != frame.height)
        {
            if (videoTexture)
                SDL_DestroyTexture(videoTexture);
            videoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                             static_cast<int>(width), static_cast<int>(frame.height));
            if (!videoTexture)
                throw std::runtime_error(SDL_GetError());
            videoWidth = width;
            videoHeight = frame.height;
            if (!SDL_SetTextureScaleMode(videoTexture, pixelArt ? SDL_SCALEMODE_PIXELART : bilinear ? SDL_SCALEMODE_LINEAR
                                                                                                    : SDL_SCALEMODE_NEAREST))
                throw std::runtime_error(SDL_GetError());
        }
        const auto *pixels = frame.rgba + crop * 4;
        int pitch = static_cast<int>(frame.pitch);
        if (settings.scanlinesEnabled)
        {
            const auto rowBytes = static_cast<std::size_t>(width) * 4;
            applyScanlines(frame.rgba, frame.pitch, width, frame.height, crop, settings.scanlineLevel, scanlinePixels);
            pixels = scanlinePixels.data();
            pitch = static_cast<int>(rowBytes);
        }
        if (!SDL_UpdateTexture(videoTexture, nullptr, pixels, pitch))
            throw std::runtime_error(SDL_GetError());
        const auto available = ImGui::GetContentRegionAvail();
        const auto layout = R2Window::videoLayout(available.x, available.y, static_cast<float>(visibleWidth),
                                                  static_cast<float>(nativeSize.height), fullscreen == FullscreenMode::Stretched);
        const ImVec2 size(layout.width, layout.height);
        if (size.x > 0 && size.y > 0)
        {
            SDL_Texture *displayTexture = videoTexture;
            bool useShader = shader != DisplayShader::None;
            std::string shaderError;
            if (useShader && (initializedShader != shader || (shader == DisplayShader::Gauss && shaderSourceWidth != frame.width)))
            {
                const char *preset = shader == DisplayShader::Gauss ? (frame.width <= 160 ? "ntsc/ntsc-160px-gauss-scanline.cgp" : "ntsc/ntsc-256px-gauss-scanline.cgp") : shader == DisplayShader::HQ2 ? "hqx/hq2x-visual.cgp"
                                                                                                                                                                       : shader == DisplayShader::CRT   ? "crt/crt-hyllian-3d-nes.cgp"
                                                                                                                                                                       : shader == DisplayShader::CRT3D ? "crt/crt-lottes.cgp"
                                                                                                                                                                                                        : "scalefx/scalefx-visual.cgp";
                auto path = std::filesystem::path(SDL_GetBasePath()) / "shaders" / preset;
                if (!std::filesystem::exists(path))
                    path = std::filesystem::path("shaders") / preset;
                auto candidate = std::make_unique<NtscShaderPipeline>();
                if (candidate->initialize(path, shaderError))
                {
                    shaderPipeline = std::move(candidate);
                    initializedShader = shader;
                    shaderSourceWidth = frame.width;
                }
                else
                    useShader = false;
            }
            if (useShader)
            {
                const auto &io = ImGui::GetIO();
                const int outputWidth = std::max(1, static_cast<int>(std::lround(size.x * io.DisplayFramebufferScale.x)));
                const int outputHeight = std::max(1, static_cast<int>(std::lround(size.y * io.DisplayFramebufferScale.y)));
                if (!shaderTexture || shaderWidth != outputWidth || shaderHeight != outputHeight)
                {
                    if (shaderTexture)
                        SDL_DestroyTexture(shaderTexture);
                    shaderTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, outputWidth, outputHeight);
                    shaderWidth = outputWidth;
                    shaderHeight = outputHeight;
                    if (!shaderTexture || !SDL_SetTextureBlendMode(shaderTexture, SDL_BLENDMODE_NONE))
                    {
                        shaderError = SDL_GetError();
                        useShader = false;
                    }
                }
                if (useShader)
                {
                    // O contrato FrameView é RGBA em bytes; a pipeline recebe ARGB em inteiros.
                    shaderInput.resize(static_cast<std::size_t>(width) * frame.height);
                    for (unsigned y = 0; y < frame.height; ++y)
                    {
                        const auto *row = pixels + static_cast<std::size_t>(y) * pitch;
                        for (unsigned x = 0; x < width; ++x)
                            shaderInput[static_cast<std::size_t>(y) * width + x] = 0xff000000u | (std::uint32_t(row[x * 4]) << 16) | (std::uint32_t(row[x * 4 + 1]) << 8) | row[x * 4 + 2];
                    }
                    if (!shaderPipeline->render(shaderInput.data(), static_cast<int>(width), {static_cast<int>(width), static_cast<int>(frame.height)},
                                                {outputWidth, outputHeight}, shaderOutput, shaderError))
                        useShader = false;
                    else
                    {
                        // ScaleFX pode retornar alfa zero; a tela do jogo é sempre opaca.
                        for (auto &pixel : shaderOutput)
                            pixel |= 0xff000000u;
                        if (!SDL_UpdateTexture(shaderTexture, nullptr, shaderOutput.data(), outputWidth * sizeof(std::uint32_t)))
                        {
                            shaderError = SDL_GetError();
                            useShader = false;
                        }
                        else
                            displayTexture = shaderTexture;
                    }
                }
            }
            if (shader != DisplayShader::None && !useShader)
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shaders", shaderError.c_str(), window);
                shader = DisplayShader::None;
            }
            // Centraliza o vídeo e mantém as barras livres quando a janela maximizada tem outra proporção.
            const auto position = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(position.x + layout.x, position.y + layout.y));
            if (callbacks.lightPhaser)
            {
                const auto origin = ImGui::GetCursorScreenPos();
                const auto mouse = ImGui::GetIO().MousePos;
                const bool inside = pointerAvailable && mouse.x >= origin.x && mouse.y >= origin.y &&
                                    mouse.x < origin.x + size.x && mouse.y < origin.y + size.y;
                const int x = inside ? static_cast<int>((mouse.x - origin.x) * visibleWidth / size.x) + static_cast<int>(crop) : -1;
                const int y = inside ? static_cast<int>((mouse.y - origin.y) * nativeSize.height / size.y) : -1;
                callbacks.lightPhaser(x, y, pointerAvailable && ImGui::IsMouseDown(ImGuiMouseButton_Left));
            }
            if (pixelArt && !useShader)
            {
                const auto origin = ImGui::GetCursorScreenPos();
                const auto viewport = ImGui::GetMainViewport()->Pos;
                PixelArtDraw draw{renderer, videoTexture, {origin.x - viewport.x, origin.y - viewport.y, size.x, size.y}};
                ImGui::GetWindowDrawList()->AddCallback(drawPixelArt, &draw, sizeof(draw));
                ImGui::Dummy(size);
            }
            else
            {
                // O backend aplica o sampler selecionado à textura de vídeo ao desenhar.
                ImGui::GetWindowDrawList()->AddCallback(bilinear ? ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear
                                                                 : ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest,
                                                        nullptr);
                ImGui::Image(reinterpret_cast<ImTextureID>(displayTexture), size);
                ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
            }
        }
    }
}