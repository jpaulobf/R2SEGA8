#include "Window.h"
#include "FileManager.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <stdexcept>
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
        bool sdlReady = false;
        bool contextReady = false;
        bool platformReady = false;
        bool rendererReady = false;
        bool running = true;
        // Encaminha ações e consultas ao seletor, sem inicializar recursos nativos.
        explicit Impl(Callbacks handlers)
            : callbacks(std::move(handlers)), files(callbacks.inspectRom, callbacks.openRom) {}
        // Notifica o encerramento uma única vez, mesmo com múltiplos eventos na fila.
        void requestExit()
        {
            if (!running)
                return;
            running = false;
            if (callbacks.exit)
                callbacks.exit();
        }
        // Libera somente os recursos cuja inicialização foi concluída.
        ~Impl()
        {
            if (rendererReady)
                ImGui_ImplSDLRenderer3_Shutdown();
            if (platformReady)
                ImGui_ImplSDL3_Shutdown();
            if (contextReady)
                ImGui::DestroyContext();
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            if (sdlReady)
                SDL_Quit();
        }
    };

    Window::Window(Callbacks callbacks) : impl_(std::make_unique<Impl>(std::move(callbacks))) {}
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
        state.window = SDL_CreateWindow("R2SEGA8", static_cast<int>(960 * scale), static_cast<int>(680 * scale),
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!state.window)
            throw std::runtime_error(SDL_GetError());
        state.renderer = SDL_CreateRenderer(state.window, nullptr);
        if (!state.renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderVSync(state.renderer, 0); // O chamador controla o ritmo de apresentação.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        state.contextReady = true;
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
    }

    void Window::requestOpen(const std::filesystem::path &path) { impl_->files.requestOpen(path); }

    bool Window::processEvents()
    {
        auto &state = *impl_;
        if (!state.rendererReady)
            throw std::logic_error("Open the window before processing events.");
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(state.window)))
                state.requestExit();
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_O &&
                (event.key.mod & SDL_KMOD_CTRL))
                state.files.requestOpen();
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
                state.files.requestOpen(std::filesystem::u8path(event.drop.data));
        }
        return state.running;
    }

    namespace
    {
        void drawBytes(const char *label, ByteView bytes)
        {
            if (!ImGui::CollapsingHeader(label))
                return;
            if (ImGui::BeginChild(label, ImVec2(0, 180), ImGuiChildFlags_Borders))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>((bytes.size + 15) / 16));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const std::size_t start = static_cast<std::size_t>(row) * 16;
                        ImGui::Text("%06X:", static_cast<unsigned>(start));
                        for (std::size_t i = start; i < std::min(start + 16, bytes.size); ++i)
                        {
                            ImGui::SameLine();
                            ImGui::Text("%02X", static_cast<unsigned>(bytes.data[i]));
                        }
                    }
                }
            }
            ImGui::EndChild();
        }
    }

    void Window::render()
    {
        auto &state = *impl_;
        if (!state.rendererReady)
            throw std::logic_error("Open the window before rendering.");
        if (SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED)
            return;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        const bool visible = ImGui::Begin("R2SEGA8", nullptr,
                                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_MenuBar);
        if (visible)
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Open...", "Ctrl+O"))
                        state.files.requestOpen();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Exit"))
                        state.requestExit();
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (state.callbacks.memory)
            {
                const auto memory = state.callbacks.memory();
                if (memory.rom.data)
                    drawBytes("Cartridge ROM (file offsets)", memory.rom);
                if (memory.ram.data)
                    drawBytes("System RAM (physical offsets)", memory.ram);
            }
            // Consome as consultas antes das ações, que podem invalidar os bytes apresentados.
            state.files.draw();
        }
        ImGui::End();
        ImGui::Render();
        const auto &io = ImGui::GetIO();
        if (!SDL_SetRenderScale(state.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) ||
            !SDL_SetRenderDrawColor(state.renderer, 18, 21, 28, 255) || !SDL_RenderClear(state.renderer))
            throw std::runtime_error(SDL_GetError());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), state.renderer);
        if (!SDL_RenderPresent(state.renderer))
            throw std::runtime_error(SDL_GetError());
    }
}