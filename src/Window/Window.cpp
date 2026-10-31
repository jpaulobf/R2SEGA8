#include "Window.h"
#include "FileManager.h"
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

namespace R2SEGA8::Window
{
    // Mantém o estado da interface e a posse dos recursos SDL3 e ImGui.
    struct Window::Impl
    {
        Callbacks callbacks;
        FileManager files;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *videoTexture = nullptr;
        unsigned videoWidth = 0, videoHeight = 0;
        DisplaySize nativeSize;
        unsigned displayScale = 3, pendingDisplayScale = 0;
        SDL_DisplayID centerDisplay = 0;
        int fixedWidth = 0, fixedHeight = 0;
        float menuHeight = 0;
        bool maximized = false;
        bool vsync = false;
        bool reportedMenuPause = false;
        std::string romName, title;
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
            fixedWidth = static_cast<int>(std::lround(nativeSize.width * displayScale / density));
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
            if (maximized || (SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)))
                return;
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window, &width, &height))
                throw std::runtime_error(SDL_GetError());
            if (width != fixedWidth || height != fixedHeight)
                if (!SDL_SetWindowSize(window, fixedWidth, fixedHeight))
                    throw std::runtime_error(SDL_GetError());
        }
        bool sdlReady = false;
        bool contextReady = false;
        bool platformReady = false;
        bool rendererReady = false;
        bool running = true, focused = false, minimized = false;
        // Consulta somente estado da interface; não conhece mapeamentos ou periféricos emulados.
        bool acceptsInput() const
        {
            if (!contextReady || !running || !focused || minimized || files.isOpen())
                return false;
            const auto &io = ImGui::GetIO();
            return !io.WantCaptureKeyboard && !io.WantTextInput &&
                   !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }
        // Publica pausa de menus e disponibilidade de entrada também nos quadros sem eventos SDL.
        void notifyInput()
        {
            const bool menuPaused = files.isOpen() ||
                                    (contextReady && ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
            if (menuPaused != reportedMenuPause)
            {
                reportedMenuPause = menuPaused;
                if (callbacks.menuPause)
                    callbacks.menuPause(menuPaused);
            }
            if (callbacks.inputAvailability)
                callbacks.inputAvailability(acceptsInput());
        }
        // Compõe FPS antes do nome da ROM e evita atualizar o título nativo quando o texto não mudou.
        void updateTitle()
        {
            const double measured = callbacks.fps ? callbacks.fps() : 0;
            const double fps = std::isfinite(measured) && measured >= 0 ? measured : 0;
            char prefix[96];
            std::snprintf(prefix, sizeof(prefix), "R2SEGA8 | FPS : %.2f", fps);
            auto next = std::string(prefix);
            if (!romName.empty())
                next += " | " + romName;
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
        // Copia um quadro neutro para a textura; os bytes do callback não escapam desta chamada.
        void drawFrame(FrameView frame)
        {
            if (!frame.rgba || !frame.width || !frame.height)
                return;
            const auto limit = static_cast<unsigned>(std::numeric_limits<int>::max());
            if (frame.width > limit / 4 || frame.height > limit || frame.pitch > limit || frame.pitch < frame.width * 4)
                throw std::invalid_argument("Invalid presentation frame dimensions or pitch.");
            if (!videoTexture || videoWidth != frame.width || videoHeight != frame.height)
            {
                if (videoTexture)
                    SDL_DestroyTexture(videoTexture);
                videoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                                 static_cast<int>(frame.width), static_cast<int>(frame.height));
                if (!videoTexture)
                    throw std::runtime_error(SDL_GetError());
                videoWidth = frame.width;
                videoHeight = frame.height;
                if (!SDL_SetTextureScaleMode(videoTexture, SDL_SCALEMODE_NEAREST))
                    throw std::runtime_error(SDL_GetError());
            }
            if (!SDL_UpdateTexture(videoTexture, nullptr, frame.rgba, static_cast<int>(frame.pitch)))
                throw std::runtime_error(SDL_GetError());
            const auto available = ImGui::GetContentRegionAvail();
            const float scale = std::min(available.x / nativeSize.width, available.y / nativeSize.height);
            const ImVec2 size(nativeSize.width * scale, nativeSize.height * scale);
            if (scale > 0)
            {
                // Centraliza o vídeo e mantém as barras livres quando a janela maximizada tem outra proporção.
                const auto position = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(position.x + (available.x - size.x) * 0.5f, position.y + (available.y - size.y) * 0.5f));
                // O backend escolhe o filtro ao desenhar; limita nearest à imagem de vídeo.
                ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
                ImGui::Image(reinterpret_cast<ImTextureID>(videoTexture), size);
                ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
            }
        }
        // Libera somente os recursos cuja inicialização foi concluída.
        ~Impl()
        {
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
            SDL_CreateWindow("R2SEGA8", static_cast<int>(state.nativeSize.width * state.displayScale), static_cast<int>(state.nativeSize.height * state.displayScale),
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
                    state.resizeDisplay(state.displayScale);
                }
                if (event.type == SDL_EVENT_WINDOW_RESIZED)
                    state.enforceDisplaySize();
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && !state.maximized)
                    state.resizeDisplay(state.displayScale);
            }
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(state.window)))
                state.requestExit();
            if (ownEvent && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F12 &&
                !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) && !state.files.isOpen())
                state.requestReset();
            // Pause/Break é uma ação da aplicação: continua disponível quando o jogo está pausado ou um menu captura o teclado.
            if (ownEvent && state.focused && !state.minimized && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.key == SDLK_PAUSE && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)) &&
                state.callbacks.togglePause && state.callbacks.hasRom && state.callbacks.hasRom())
                state.callbacks.togglePause();
            if (ownEvent && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.mod & SDL_KMOD_CTRL) &&
                !(event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) && event.key.key >= SDLK_1 && event.key.key <= SDLK_5)
                state.resizeDisplay(static_cast<unsigned>(event.key.key - SDLK_1 + 1));
            if (ownEvent && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_O &&
                (event.key.mod & SDL_KMOD_CTRL))
                state.files.requestOpen();
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
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
        state.updateTitle();
        if (SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED)
        {
            state.minimized = true;
            state.notifyInput();
            return;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool visible = ImGui::Begin("R2SEGA8", nullptr,
                                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        if (visible)
        {
            if (ImGui::BeginMenuBar())
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
                    if (ImGui::MenuItem("Exit"))
                        state.requestExit();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Display"))
                {
                    const bool configurable = state.callbacks.executionSettings && state.callbacks.setExecutionSettings;
                    auto settings = configurable ? state.callbacks.executionSettings() : Common::ExecutionSettings{};
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
                if (ImGui::BeginMenu("Hacks", state.callbacks.executionSettings && state.callbacks.setExecutionSettings))
                {
                    auto settings = state.callbacks.executionSettings();
                    if (ImGui::MenuItem("Enable Fast-Forward", "Tab (hold)", &settings.fastForwardEnabled))
                        state.callbacks.setExecutionSettings(settings);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (state.callbacks.frame)
                state.drawFrame(state.callbacks.frame());
            // Consome as consultas antes das ações, que podem invalidar os bytes apresentados.
            state.files.draw();
        }
        ImGui::End();
        ImGui::Render();
        state.notifyInput();
        const auto &io = ImGui::GetIO();
        if (!SDL_SetRenderScale(state.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) ||
            !SDL_SetRenderDrawColor(state.renderer, 18, 21, 28, 255) || !SDL_RenderClear(state.renderer))
            throw std::runtime_error(SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), state.renderer);
        if (!SDL_RenderPresent(state.renderer))
            throw std::runtime_error(SDL_GetError());
        // Aplica ações do menu depois de apresentar o quadro com a geometria usada durante seu desenho.
        if (state.pendingDisplayScale)
        {
            const auto scale = state.pendingDisplayScale;
            state.pendingDisplayScale = 0;
            state.resizeDisplay(scale);
        }
    }
}