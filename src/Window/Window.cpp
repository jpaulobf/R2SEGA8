#include "Window.h"
#include "FileManager.h"
#include "DebugWindow.h"
#include "NtscShaderPipeline.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// commctrl depende dos tipos Win32; este bloco deve permanecer depois de windows.h.
#include <commctrl.h>
#endif
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace R2SEGA8::Window
{
    // Mantém o estado da interface e a posse dos recursos SDL3 e ImGui.
    struct Window::Impl
    {
        Callbacks callbacks;
        FileManager files;
        std::unique_ptr<DebugWindow> debugger;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *videoTexture = nullptr;
        SDL_Texture *shaderTexture = nullptr;
        enum class Shader
        {
            None,
            Gauss,
            HQ2,
            CRT,
            CRT3D,
            ScaleFX
        };
        Shader shader = Shader::None, initializedShader = Shader::None;
        unsigned shaderSourceWidth = 0;
        int shaderWidth = 0, shaderHeight = 0;
        std::unique_ptr<NtscShaderPipeline> shaderPipeline;
        std::vector<std::uint32_t> shaderInput, shaderOutput;
        unsigned videoWidth = 0, videoHeight = 0;
        std::vector<std::uint8_t> scanlinePixels;
        DisplaySize nativeSize;
        unsigned displayScale = 3, pendingDisplayScale = 0;
        enum class Fullscreen
        {
            None,
            Stretched,
            Aspect
        };
        Fullscreen fullscreen = Fullscreen::None, pendingFullscreen = Fullscreen::None;
        unsigned stateSlot = 1;
        SDL_DisplayID centerDisplay = 0;
        int fixedWidth = 0, fixedHeight = 0;
        float menuHeight = 0;
        bool maximized = false;
        bool vsync = false;
        bool reportedMenuPause = false;
        std::string romName;
        std::string title;

        unsigned displayWidth() const
        {
            const bool crop = callbacks.executionSettings && callbacks.executionSettings().cropLeftEnabled;
            return crop && nativeSize.width > 8 ? nativeSize.width - 8 : nativeSize.width;
        }

#ifdef _WIN32
        HWND nativeWindow = nullptr;
        bool resizeHook = false;
        // Preserva os comandos de maximização e bloqueia somente bordas e o comando nativo de redimensionar.
        static LRESULT CALLBACK fixedWindowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
        {
            if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_SIZE)
                return 0;
            const auto result = DefSubclassProc(handle, message, wparam, lparam);
            if (message == WM_NCHITTEST && result >= HTLEFT && result <= HTBOTTOMRIGHT)
                return HTBORDER;
            return result;
        }
#endif
        // Aplica a escala à área de vídeo, acrescenta o menu e centraliza no monitor atual, inclusive após restaurar.
        void resizeDisplay(unsigned scale)
        {
            displayScale = scale;
            if (!centerDisplay)
            {
                centerDisplay = SDL_GetDisplayForWindow(window);
                if (!centerDisplay)
                    throw std::runtime_error(SDL_GetError());
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)
            {
                if (!SDL_RestoreWindow(window))
                    throw std::runtime_error(SDL_GetError());
                return; // A restauração pode ser assíncrona; o evento RESTORED conclui o tamanho solicitado.
            }
            maximized = false;
            const float density = std::max(1.0f, SDL_GetWindowPixelDensity(window));
            fixedWidth = static_cast<int>(std::lround(displayWidth() * displayScale / density));
            fixedHeight = static_cast<int>(std::lround(nativeSize.height * displayScale / density + menuHeight));
            if (!SDL_SetWindowSize(window, fixedWidth, fixedHeight))
                throw std::runtime_error(SDL_GetError());
            const auto centered = SDL_WINDOWPOS_CENTERED_DISPLAY(centerDisplay);
            if (!SDL_SetWindowPosition(window, centered, centered))
                throw std::runtime_error(SDL_GetError());
            centerDisplay = 0;
        }
        // Corrige tamanhos arbitrários sem impor limites que impeçam a maximização do gerenciador de janelas.
        void enforceDisplaySize()
        {
            if (fullscreen != Fullscreen::None || maximized || (SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)))
                return;
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window, &width, &height))
                throw std::runtime_error(SDL_GetError());
            if (width != fixedWidth || height != fixedHeight)
                if (!SDL_SetWindowSize(window, fixedWidth, fixedHeight))
                    throw std::runtime_error(SDL_GetError());
        }
        void setFullscreen(Fullscreen mode)
        {
            if (mode != Fullscreen::None && !SDL_SetWindowFullscreenMode(window, nullptr))
                throw std::runtime_error(SDL_GetError());
            if (!SDL_SetWindowFullscreen(window, mode != Fullscreen::None))
                throw std::runtime_error(SDL_GetError());
            fullscreen = mode;
            if (mode == Fullscreen::None)
            {
                if (!SDL_SyncWindow(window))
                    throw std::runtime_error(SDL_GetError());
                resizeDisplay(displayScale);
            }
        }
        bool sdlReady = false;
        bool contextReady = false;
        bool platformReady = false;
        bool rendererReady = false;
        bool running = true, focused = false, minimized = false;
        // Consulta somente estado da interface; não conhece mapeamentos ou periféricos emulados.
        bool acceptsPointer() const
        {
            if (!contextReady || !running || !focused || minimized || files.isOpen())
                return false;
            if (fullscreen != Fullscreen::None)
                return true;
            return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }
        bool acceptsInput() const
        {
            if (!acceptsPointer())
                return false;
            if (fullscreen != Fullscreen::None)
                return true;
            const auto &io = ImGui::GetIO();
            return !io.WantCaptureKeyboard && !io.WantTextInput;
        }
        // Publica pausa de menus e disponibilidade de entrada também nos quadros sem eventos SDL.
        void notifyInput()
        {
            const bool menuPaused = fullscreen == Fullscreen::None && (files.isOpen() ||
                                                                       (contextReady && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)));
            if (menuPaused != reportedMenuPause)
            {
                reportedMenuPause = menuPaused;
                if (callbacks.menuPause)
                    callbacks.menuPause(menuPaused);
            }
            if (callbacks.inputAvailability)
                callbacks.inputAvailability(acceptsInput());
            if (callbacks.lightPhaser && !acceptsPointer())
                callbacks.lightPhaser(-1, -1, false);
        }
        // Compõe FPS antes do nome da ROM e evita atualizar o título nativo quando o texto não mudou.
        void updateTitle()
        {
            const double measured = callbacks.fps ? callbacks.fps() : 0;
            const double fps = std::isfinite(measured) && measured >= 0 ? measured : 0;
            char prefix[96];
            std::snprintf(prefix, sizeof(prefix), "%s | FPS : %.2f", emuName, fps);
            auto next = std::string(prefix);
            if (!romName.empty())
                next += " | " + romName;
            if (callbacks.executionSettings && callbacks.executionSettings().cpuOverclockEnabled)
                next += " | CPU OVERCLOCK enabled... Games can crash!!";
            if (next != title)
            {
                SDL_SetWindowTitle(window, next.c_str());
                title = std::move(next);
            }
        }
        // Encaminha ações e consultas ao seletor, sem inicializar recursos nativos.
        explicit Impl(Callbacks handlers, DisplaySize size)
            : callbacks(std::move(handlers)),
              files([this](const std::filesystem::path &path, Common::SystemType type)
                    {
                        if (!callbacks.openRom)
                            throw std::runtime_error("ROM loading callback is not configured.");
                        const auto name = path.filename().u8string();
                        callbacks.openRom(path, type);
                        // Atualiza somente após a aplicação confirmar o carregamento sem lançar exceção.
                        romName = name;
                        updateTitle(); }),
              nativeSize(size)
        {
            const auto limit = static_cast<unsigned>(std::numeric_limits<int>::max() / 10);
            if (!size.width || !size.height || size.width > limit || size.height > limit)
                throw std::invalid_argument("Invalid native presentation size.");
        }
        // Notifica o encerramento uma única vez, mesmo com múltiplos eventos na fila.
        void requestExit()
        {
            if (!running)
                return;
            running = false;
            if (callbacks.exit)
                callbacks.exit();
        }

        // Solicita reinicialização somente quando a aplicação informa uma ROM ativa.
        void requestReset()
        {
            if (callbacks.reset && callbacks.hasRom && callbacks.hasRom())
                callbacks.reset();
        }

        // Descarrega pela aplicação e restaura o título após a conclusão da ação.
        void requestUnload()
        {
            if (callbacks.unload && callbacks.hasRom && callbacks.hasRom())
            {
                callbacks.unload();
                romName.clear();
                updateTitle();
            }
        }
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

        // Copia um quadro neutro para a textura; os bytes do callback não escapam desta chamada.
        void drawFrame(FrameView frame)
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
                scanlinePixels.resize(rowBytes * frame.height);
                const auto level = std::clamp(settings.scanlineLevel, 5u, 25u);
                for (unsigned y = 0; y < frame.height; ++y)
                {
                    const auto *source = frame.rgba + static_cast<std::size_t>(y) * frame.pitch + crop * 4;
                    auto *destination = scanlinePixels.data() + static_cast<std::size_t>(y) * rowBytes;
                    std::copy_n(source, rowBytes, destination);
                    if (y % 2 != 0)
                        for (std::size_t x = 0; x < rowBytes; x += 4)
                            for (unsigned channel = 0; channel < 3; ++channel)
                                destination[x + channel] = static_cast<std::uint8_t>(destination[x + channel] * (100 - level) / 100);
                }
                pixels = scanlinePixels.data();
                pitch = static_cast<int>(rowBytes);
            }
            if (!SDL_UpdateTexture(videoTexture, nullptr, pixels, pitch))
                throw std::runtime_error(SDL_GetError());
            const auto available = ImGui::GetContentRegionAvail();
            const auto visibleWidth = displayWidth();
            const float scale = std::min(available.x / visibleWidth, available.y / nativeSize.height);
            const ImVec2 size = fullscreen == Fullscreen::Stretched ? available : ImVec2(visibleWidth * scale, nativeSize.height * scale);
            if (scale > 0)
            {
                SDL_Texture *displayTexture = videoTexture;
                bool useShader = shader != Shader::None;
                std::string shaderError;
                if (useShader && (initializedShader != shader || (shader == Shader::Gauss && shaderSourceWidth != frame.width)))
                {
                    const char *preset = shader == Shader::Gauss ? (frame.width <= 160 ? "ntsc/ntsc-160px-gauss-scanline.cgp" : "ntsc/ntsc-256px-gauss-scanline.cgp") : shader == Shader::HQ2 ? "hqx/hq2x-visual.cgp"
                                                                                                                                                                    : shader == Shader::CRT   ? "crt/crt-hyllian-3d-nes.cgp"
                                                                                                                                                                    : shader == Shader::CRT3D ? "crt/crt-lottes.cgp"
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
                if (shader != Shader::None && !useShader)
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shaders", shaderError.c_str(), window);
                    shader = Shader::None;
                }
                // Centraliza o vídeo e mantém as barras livres quando a janela maximizada tem outra proporção.
                const auto position = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(position.x + (available.x - size.x) * 0.5f, position.y + (available.y - size.y) * 0.5f));
                if (callbacks.lightPhaser)
                {
                    const auto origin = ImGui::GetCursorScreenPos();
                    const auto mouse = ImGui::GetIO().MousePos;
                    const bool inside = acceptsPointer() && mouse.x >= origin.x && mouse.y >= origin.y &&
                                        mouse.x < origin.x + size.x && mouse.y < origin.y + size.y;
                    const int x = inside ? static_cast<int>((mouse.x - origin.x) * visibleWidth / size.x) + static_cast<int>(crop) : -1;
                    const int y = inside ? static_cast<int>((mouse.y - origin.y) * nativeSize.height / size.y) : -1;
                    callbacks.lightPhaser(x, y, acceptsPointer() && ImGui::IsMouseDown(ImGuiMouseButton_Left));
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
        // Libera somente os recursos cuja inicialização foi concluída.
        ~Impl()
        {
            debugger.reset();
#ifdef _WIN32
            if (resizeHook)
                RemoveWindowSubclass(nativeWindow, fixedWindowProc, 1);
#endif
            if (rendererReady)
                ImGui_ImplSDLRenderer3_Shutdown();
            if (platformReady)
                ImGui_ImplSDL3_Shutdown();
            if (contextReady)
                ImGui::DestroyContext();
            if (videoTexture)
                SDL_DestroyTexture(videoTexture);
            if (shaderTexture)
                SDL_DestroyTexture(shaderTexture);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            if (sdlReady)
                SDL_Quit();
        }
    };

    Window::Window(Callbacks callbacks, DisplaySize nativeSize) : impl_(std::make_unique<Impl>(std::move(callbacks), nativeSize)) {}
    Window::~Window() = default;

    void Window::open()
    {
        auto &state = *impl_;
        if (state.sdlReady)
            throw std::logic_error("Window session has already been initialized.");
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        state.sdlReady = true;
        const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
        state.window =
            SDL_CreateWindow(emuName, static_cast<int>(state.nativeSize.width * state.displayScale), static_cast<int>(state.nativeSize.height * state.displayScale),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!state.window)
            throw std::runtime_error(SDL_GetError());
#ifdef _WIN32
        state.nativeWindow =
            static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(state.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (state.nativeWindow)
        {
            if (!SetWindowSubclass(state.nativeWindow, Impl::fixedWindowProc, 1, 0))
                throw std::runtime_error("Unable to install window sizing policy.");
            state.resizeHook = true;
        }
#endif
        state.focused = (SDL_GetWindowFlags(state.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        state.renderer = SDL_CreateRenderer(state.window, nullptr);
        if (!state.renderer)
            throw std::runtime_error(SDL_GetError());
        if (!SDL_SetRenderVSync(state.renderer, 0))
            throw std::runtime_error(SDL_GetError());
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state.contextReady = true;
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Navegação ociosa não captura as setas do jogo; popups e campos ativos continuam bloqueando entrada.
        io.ConfigNavCaptureKeyboard = false;
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleDpi = scale;
        if (!ImGui_ImplSDL3_InitForSDLRenderer(state.window, state.renderer))
            throw std::runtime_error("Unable to initialize the ImGui SDL3 backend.");
        state.platformReady = true;
        if (!ImGui_ImplSDLRenderer3_Init(state.renderer))
            throw std::runtime_error("Unable to initialize the ImGui renderer backend.");
        state.rendererReady = true;
        // Mede a fonte real antes de definir o tamanho inicial, incluindo o menu sem reduzir a área de vídeo.
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        state.menuHeight = ImGui::GetFrameHeight();
        ImGui::EndFrame();
        state.resizeDisplay(state.displayScale);
        state.updateTitle();
        state.debugger = std::make_unique<DebugWindow>(state.callbacks, state.window);
    }

    void Window::requestOpen(const std::filesystem::path &path)
    {
        impl_->files.requestOpen(path);
    }

    bool Window::setVSync(bool enabled)
    {
        auto &state = *impl_;
        if (!state.rendererReady)
            throw std::logic_error("Open the window before configuring VSync.");
        if (state.vsync == enabled)
            return true;
        if (!SDL_SetRenderVSync(state.renderer, enabled ? 1 : 0))
            return false;
        state.vsync = enabled;
        return true;
    }

    bool Window::isMinimized() const
    {
        const auto &state = *impl_;
        return !state.window || state.minimized || (SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED) != 0;
    }

    bool Window::processEvents()
    {
        auto &state = *impl_;
        if (!state.rendererReady)
            throw std::logic_error("Open the window before processing events.");
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (state.debugger && state.debugger->processEvent(event))
                continue;
            ImGui_ImplSDL3_ProcessEvent(&event);
            const auto ownID = SDL_GetWindowID(state.window);
            const bool windowEvent = event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST;
            const bool keyEvent = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
            const bool ownEvent =
                (!windowEvent || event.window.windowID == ownID) && (!keyEvent || event.key.windowID == 0 || event.key.windowID == ownID);
            if (ownEvent)
            {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    state.focused = true;
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    state.focused = false;
                if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                    state.minimized = true;
                if (event.type == SDL_EVENT_WINDOW_MAXIMIZED)
                {
                    state.maximized = true;
                    state.minimized = false;
                }
                if (event.type == SDL_EVENT_WINDOW_RESTORED)
                {
                    state.minimized = false;
                    state.maximized = false;
                    if (state.fullscreen == Impl::Fullscreen::None)
                        state.resizeDisplay(state.displayScale);
                }
                if (event.type == SDL_EVENT_WINDOW_RESIZED)
                    state.enforceDisplaySize();
                if (state.debugger && (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED ||
                                       event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED))
                    state.debugger->snapToParent();
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && !state.maximized && state.fullscreen == Impl::Fullscreen::None)
                    state.resizeDisplay(state.displayScale);
            }
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(state.window)))
                state.requestExit();
            if (ownEvent && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F12 &&
                !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) && !state.files.isOpen())
                state.requestReset();
            if (ownEvent && state.focused && !state.minimized && !state.files.isOpen() && event.type == SDL_EVENT_KEY_DOWN &&
                !event.key.repeat && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) &&
                state.callbacks.canSaveState && state.callbacks.canSaveState())
            {
                if (event.key.key == SDLK_F5 && state.callbacks.saveState)
                    state.callbacks.saveState(state.stateSlot);
                if (event.key.key == SDLK_F6 && state.callbacks.loadState)
                    state.callbacks.loadState(state.stateSlot);
            }
            // Pause/Break é uma ação da aplicação: continua disponível quando o jogo está pausado ou um menu captura o teclado.
            if (ownEvent && state.focused && !state.minimized && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.key == SDLK_PAUSE && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) &&
                state.callbacks.togglePause && state.callbacks.hasRom && state.callbacks.hasRom())
                state.callbacks.togglePause();
            if (ownEvent && state.fullscreen != Impl::Fullscreen::None && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.key == SDLK_ESCAPE)
            {
                state.setFullscreen(Impl::Fullscreen::None);
                continue;
            }
            if (ownEvent && state.focused && !state.minimized && !state.files.isOpen() && event.type == SDL_EVENT_KEY_DOWN &&
                !event.key.repeat && event.key.key == SDLK_F && (event.key.mod & SDL_KMOD_CTRL) &&
                !(event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)))
            {
                state.setFullscreen(state.fullscreen == Impl::Fullscreen::None ? Impl::Fullscreen::Aspect : Impl::Fullscreen::None);
                continue;
            }
            if (ownEvent && state.fullscreen == Impl::Fullscreen::None && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.mod & SDL_KMOD_CTRL) &&
                !(event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) && event.key.key >= SDLK_1 && event.key.key <= SDLK_5)
                state.resizeDisplay(static_cast<unsigned>(event.key.key - SDLK_1 + 1));
            if (ownEvent && state.fullscreen == Impl::Fullscreen::None && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_O &&
                (event.key.mod & SDL_KMOD_CTRL))
                state.files.requestOpen();
            if (state.fullscreen == Impl::Fullscreen::None && event.type == SDL_EVENT_DROP_FILE && event.drop.data)
                state.files.requestOpen(std::filesystem::u8path(event.drop.data));
            if (ownEvent && state.callbacks.inputEvent)
                state.callbacks.inputEvent(event, state.acceptsInput());
        }
        state.notifyInput();
        return state.running;
    }

    void Window::render()
    {
        auto &state = *impl_;
        if (!state.rendererReady)
            throw std::logic_error("Open the window before rendering.");
        // A geometria acompanha o console carregado e preserva a escala escolhida.
        if (state.callbacks.presentationSize)
        {
            const auto size = state.callbacks.presentationSize();
            if (size.first && size.second && (size.first != state.nativeSize.width || size.second != state.nativeSize.height))
            {
                state.nativeSize = {size.first, size.second};
                if (state.fullscreen == Impl::Fullscreen::None && !state.maximized &&
                    !(SDL_GetWindowFlags(state.window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)))
                    state.resizeDisplay(state.displayScale);
            }
        }
        state.updateTitle();
        if (SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED)
        {
            state.minimized = true;
            state.notifyInput();
            if (state.debugger)
                state.debugger->render();
            return;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto *viewport = ImGui::GetMainViewport();
        const bool fullscreen = state.fullscreen != Impl::Fullscreen::None;
        ImGui::SetNextWindowPos(fullscreen ? viewport->Pos : viewport->WorkPos);
        ImGui::SetNextWindowSize(fullscreen ? viewport->Size : viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool visible = ImGui::Begin("R2SEGA8", nullptr,
                                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              (fullscreen ? 0 : ImGuiWindowFlags_MenuBar) | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        if (visible)
        {
            if (!fullscreen && ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Open...", "Ctrl+O"))
                        state.files.requestOpen();
                    const bool loaded = state.callbacks.hasRom && state.callbacks.hasRom();
                    if (ImGui::MenuItem("Reset", "F12", false, loaded && static_cast<bool>(state.callbacks.reset)))
                        state.requestReset();
                    if (ImGui::MenuItem("Unload", nullptr, false, loaded && static_cast<bool>(state.callbacks.unload)))
                        state.requestUnload();
                    ImGui::Separator();
                    const bool canState = state.callbacks.canSaveState && state.callbacks.canSaveState();
                    if (ImGui::MenuItem("Save State", "F5", false, canState && static_cast<bool>(state.callbacks.saveState)))
                        state.callbacks.saveState(state.stateSlot);
                    if (ImGui::MenuItem("Load State", "F6", false, canState && static_cast<bool>(state.callbacks.loadState)))
                        state.callbacks.loadState(state.stateSlot);
                    if (ImGui::BeginMenu("Save State Slot", canState))
                    {
                        for (unsigned slot = 1; slot <= 3; ++slot)
                        {
                            const auto label = std::string("Slot ") + std::to_string(slot);
                            if (ImGui::MenuItem(label.c_str(), nullptr, state.stateSlot == slot))
                                state.stateSlot = slot;
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Recent Files", !state.files.recentFiles().empty()))
                    {
                        const auto &recent = state.files.recentFiles();
                        for (std::size_t i = 0; i < recent.size(); ++i)
                        {
                            ImGui::PushID(static_cast<int>(i));
                            const auto name = recent[i].filename().u8string();
                            if (ImGui::MenuItem(name.c_str()))
                                state.files.requestOpen(recent[i]);
                            ImGui::PopID();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit"))
                        state.requestExit();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Display"))
                {
                    const bool configurable = state.callbacks.executionSettings && state.callbacks.setExecutionSettings;
                    auto settings = configurable ? state.callbacks.executionSettings() : Common::ExecutionSettings{};
                    if (ImGui::BeginMenu("Console Region", configurable &&
                                                               (!state.callbacks.consoleProfileAvailable || state.callbacks.consoleProfileAvailable())))
                    {
                        for (const auto &option : {std::pair<const char *, Common::ConsoleProfile>{"Auto", Common::ConsoleProfile::Auto},
                                                   {"NTSC", Common::ConsoleProfile::NTSC},
                                                   {"PAL", Common::ConsoleProfile::PAL}})
                            if (ImGui::MenuItem(option.first, nullptr, settings.consoleProfile == option.second))
                            {
                                settings.consoleProfile = option.second;
                                state.callbacks.setExecutionSettings(settings);
                            }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("VSync", nullptr, &settings.vsync, configurable))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::Separator();
                    for (unsigned scale = 1; scale <= 5; ++scale)
                    {
                        char label[] = "1X", shortcut[] = "Ctrl+1";
                        label[0] = static_cast<char>('0' + scale);
                        shortcut[5] = static_cast<char>('0' + scale);
                        if (ImGui::MenuItem(label, shortcut, state.displayScale == scale))
                            state.pendingDisplayScale = scale;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Borderless Fullscreen Stretched"))
                        state.pendingFullscreen = Impl::Fullscreen::Stretched;
                    if (ImGui::MenuItem("Borderless Fullscreen", "Ctrl+F"))
                        state.pendingFullscreen = Impl::Fullscreen::Aspect;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Bilinear Filter", nullptr, &settings.bilinearFilterEnabled, configurable))
                    {
                        if (settings.bilinearFilterEnabled)
                            settings.pixelArtEnabled = false;
                        state.callbacks.setExecutionSettings(settings);
                    }
                    if (ImGui::MenuItem("Pixel Art", nullptr, &settings.pixelArtEnabled, configurable))
                    {
                        if (settings.pixelArtEnabled)
                            settings.bilinearFilterEnabled = false;
                        state.callbacks.setExecutionSettings(settings);
                    }
                    if (ImGui::MenuItem("Scanlines", nullptr, &settings.scanlinesEnabled, configurable))
                        state.callbacks.setExecutionSettings(settings);
                    if (ImGui::BeginMenu("Scanline Levels", configurable && settings.scanlinesEnabled))
                    {
                        for (unsigned level = 5; level <= 25; level += 5)
                        {
                            const auto label = std::to_string(level) + "%";
                            if (ImGui::MenuItem(label.c_str(), nullptr, settings.scanlineLevel == level))
                            {
                                settings.scanlineLevel = level;
                                state.callbacks.setExecutionSettings(settings);
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Crop 8px from Left", nullptr, &settings.cropLeftEnabled, configurable))
                    {
                        state.callbacks.setExecutionSettings(settings);
                        state.pendingDisplayScale = state.displayScale;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Render sprites", nullptr, &settings.renderSprites, configurable))
                        state.callbacks.setExecutionSettings(settings);
                    if (ImGui::MenuItem("Render tiles", nullptr, &settings.renderTiles, configurable))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::Separator();
#ifdef _WIN32
                    if (ImGui::BeginMenu("Shaders"))
                    {
                        for (const auto &option : {std::pair<Impl::Shader, const char *>{Impl::Shader::None, "None"},
                                                   {Impl::Shader::Gauss, "Gauss Scanlines"},
                                                   {Impl::Shader::HQ2, "HQ2"},
                                                   {Impl::Shader::CRT, "CRT"},
                                                   {Impl::Shader::CRT3D, "CRT 3D"},
                                                   {Impl::Shader::ScaleFX, "ScaleFX"}})
                            if (ImGui::MenuItem(option.second, nullptr, state.shader == option.first))
                                state.shader = option.first;
                        ImGui::EndMenu();
                    }
#endif
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Audio", state.callbacks.audioSettings && state.callbacks.setAudioSettings))
                {
                    auto settings = state.callbacks.audioSettings();
                    const char *labels[] = {"Master Sound", "Tone 1", "Tone 2", "Tone 3", "Noise"};
                    bool changed = false;
                    // Mantém o menu aberto para ajustar vários canais, como no mixer do R2BOY.
                    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
                    for (unsigned index = 0; index < 5; ++index)
                    {
                        if (index)
                            ImGui::Separator();
                        ImGui::PushID(static_cast<int>(index));
                        auto &control = index ? settings.channels[index - 1] : settings.master;
                        changed |= ImGui::MenuItem(labels[index], nullptr, &control.enabled);
                        ImGui::SetNextItemWidth(160.0f);
                        changed |= ImGui::SliderInt("Volume", &control.volume, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::PopID();
                    }
                    ImGui::PopItemFlag();
                    if (changed)
                        state.callbacks.setAudioSettings(settings);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Input", state.callbacks.executionSettings && state.callbacks.setExecutionSettings))
                {
                    auto settings = state.callbacks.executionSettings();
                    if (ImGui::MenuItem("Enable Light Phaser", nullptr, &settings.lightPhaserEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    if (ImGui::MenuItem("Invert buttons: 1 and 2", nullptr, &settings.invert12Enabled))
                    {
                        settings.invertT1112Enabled = false;
                        state.callbacks.setExecutionSettings(settings);
                    }

                    if (ImGui::MenuItem("Change buttons: T1 goes 1, T2 goes T1, 2 goes T2, 1 goes 2", nullptr, &settings.invertT1112Enabled))
                    {
                        settings.invert12Enabled = false;
                        state.callbacks.setExecutionSettings(settings);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Debug"))
                {
                    if (ImGui::MenuItem("Disassembler", nullptr, state.debugger && state.debugger->isOpen()))
                    {
                        if (state.debugger->isOpen())
                            state.debugger->close();
                        else
                        {
                            state.debugger->open();
                            if (state.callbacks.debugOpen)
                                state.callbacks.debugOpen();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Hacks", state.callbacks.executionSettings && state.callbacks.setExecutionSettings))
                {
                    auto settings = state.callbacks.executionSettings();
                    if (ImGui::MenuItem("Enable Fast-Forward", "Tab / R2 (hold)", &settings.fastForwardEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    if (ImGui::MenuItem("Enable Rewind", "Caps Lock / L2 (hold)", &settings.rewindEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    if (ImGui::BeginMenu("Rewind Level", settings.rewindEnabled))
                    {
                        using R2SEGA8::Common::RewindLevel;
                        for (const auto &option : {std::pair{"Light", RewindLevel::Light},
                                                   std::pair{"Normal", RewindLevel::Normal},
                                                   std::pair{"Precise", RewindLevel::Precise}})
                            if (ImGui::MenuItem(option.first, nullptr, settings.rewindLevel == option.second))
                            {
                                settings.rewindLevel = option.second;
                                state.callbacks.setExecutionSettings(settings);
                            }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Enable Unlimited Sprites", nullptr, &settings.unlimitedSpritesEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Enable CPU Overclock", nullptr, &settings.cpuOverclockEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::BeginDisabled(!settings.cpuOverclockEnabled);
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::SliderFloat("CPU Multiplier", &settings.cpuMultiplier, 1.0f, 2.0f, "%.2fx"))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::EndDisabled();
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (state.callbacks.frame)
                state.drawFrame(state.callbacks.frame());
            // Consome as consultas antes das ações, que podem invalidar os bytes apresentados.
            if (!fullscreen)
                state.files.draw();
        }
        ImGui::End();
        ImGui::Render();
        state.notifyInput();
        const auto &io = ImGui::GetIO();
        if (!SDL_SetRenderScale(state.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) ||
            !SDL_SetRenderDrawColor(state.renderer, fullscreen ? 0 : 18, fullscreen ? 0 : 21, fullscreen ? 0 : 28, 255) || !SDL_RenderClear(state.renderer))
            throw std::runtime_error(SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), state.renderer);
        if (!SDL_RenderPresent(state.renderer))
            throw std::runtime_error(SDL_GetError());
        if (state.debugger)
            state.debugger->render();
        // Aplica ações do menu depois de apresentar o quadro com a geometria usada durante seu desenho.
        if (state.pendingDisplayScale)
        {
            const auto scale = state.pendingDisplayScale;
            state.pendingDisplayScale = 0;
            state.resizeDisplay(scale);
        }
        if (state.pendingFullscreen != Impl::Fullscreen::None)
        {
            const auto mode = state.pendingFullscreen;
            state.pendingFullscreen = Impl::Fullscreen::None;
            state.setFullscreen(mode);
            state.notifyInput();
        }
    }
}