#include "DebugWindow.h"
#include "Core/CPU/Z80/Disassembler.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <stdexcept>
#include <cstdio>
#include <string>

namespace R2SEGA8::Window
{
    struct DebugWindow::Impl
    {
        Callbacks &cb;
        SDL_Window *parent = nullptr;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        ImGuiContext *context = nullptr;
        bool platform = false, backend = false;
        char address[5] = "0000";
        explicit Impl(Callbacks &c, SDL_Window *p) : cb(c), parent(p) {}
        void close()
        {
            auto *previous = ImGui::GetCurrentContext();
            auto *destroyed = context;
            if (context)
            {
                ImGui::SetCurrentContext(context);
                if (backend)
                    ImGui_ImplSDLRenderer3_Shutdown();
                if (platform)
                    ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext(context);
            }
            context = nullptr;
            backend = platform = false;
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
            renderer = nullptr;
            window = nullptr;
            ImGui::SetCurrentContext(previous == destroyed ? nullptr : previous);
            if (cb.debugClose)
                cb.debugClose();
        }
    };
    DebugWindow::DebugWindow(Callbacks &c, SDL_Window *parent) : impl_(std::make_unique<Impl>(c, parent)) {}
    DebugWindow::~DebugWindow()
    {
        if (impl_->window)
            impl_->close();
    }
    bool DebugWindow::isOpen() const { return impl_->window != nullptr; }
    void DebugWindow::close()
    {
        if (isOpen())
            impl_->close();
    }
    void DebugWindow::open()
    {
        if (isOpen())
            return;
        auto &s = *impl_;
        auto *previous = ImGui::GetCurrentContext();
        s.window = SDL_CreateWindow("R2SEGA8 - Disassembler", 375, 550, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!s.window)
            throw std::runtime_error(SDL_GetError());
        s.renderer = SDL_CreateRenderer(s.window, nullptr);
        if (!s.renderer)
        {
            s.close();
            throw std::runtime_error(SDL_GetError());
        }
        s.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(s.context);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        s.platform = ImGui_ImplSDL3_InitForSDLRenderer(s.window, s.renderer);
        s.backend = s.platform && ImGui_ImplSDLRenderer3_Init(s.renderer);
        if (!s.backend)
        {
            s.close();
            ImGui::SetCurrentContext(previous);
            throw std::runtime_error("Unable to initialize debugger ImGui backend.");
        }
        ImGui::SetCurrentContext(previous);
        snapToParent();
    }
    void DebugWindow::snapToParent()
    {
        auto &s = *impl_;
        if (!s.window || !s.parent)
            return;
        int x = 0, y = 0, width = 0, mainTop = 0, mainRight = 0, debugTop = 0, debugLeft = 0;
        if (!SDL_GetWindowPosition(s.parent, &x, &y) || !SDL_GetWindowSize(s.parent, &width, nullptr))
            return;
        SDL_GetWindowBordersSize(s.parent, &mainTop, nullptr, nullptr, &mainRight);
        SDL_GetWindowBordersSize(s.window, &debugTop, &debugLeft, nullptr, nullptr);
        SDL_SetWindowPosition(s.window, x + width + mainRight + debugLeft + 1, y - mainTop + debugTop);
    }
    bool DebugWindow::processEvent(const SDL_Event &event)
    {
        auto &s = *impl_;
        if (!s.window)
            return false;
        auto id = SDL_GetWindowID(s.window);
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == id)
        {
            close();
            return true;
        }
        auto *previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(s.context);
        ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui::SetCurrentContext(previous);
        return false;
    }
    void DebugWindow::render()
    {
        auto &s = *impl_;
        if (!s.window || (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MINIMIZED))
            return;
        auto *previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(s.context);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto *v = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(v->WorkPos);
        ImGui::SetNextWindowSize(v->WorkSize);
        bool open = true;
        ImGui::Begin("Disassembler", &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        const auto view = s.cb.debugView ? s.cb.debugView() : DebugView{};
        if (!view.available)
            ImGui::TextUnformatted("Load a ROM with a supported memory map.");
        else
        {
            if (ImGui::Button(view.paused ? "Continue" : "Pause") && s.cb.debugPause)
                s.cb.debugPause(!view.paused);
            ImGui::SameLine();
            ImGui::BeginDisabled(!view.paused);
            if (ImGui::Button("Next instruction") && s.cb.debugStep)
                s.cb.debugStep();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(view.paused ? "Paused" : "Running");
            ImGui::SetNextItemWidth(75);
            ImGui::InputText("Address (hex)", s.address, sizeof s.address, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
            ImGui::SameLine();
            if (ImGui::Button("Toggle breakpoint") && s.cb.debugBreakpoint)
            {
                unsigned a = 0;
                if (std::sscanf(s.address, "%x", &a) == 1)
                    s.cb.debugBreakpoint(static_cast<std::uint16_t>(a));
            }
            if (ImGui::Button("Clear breakpoints") && s.cb.debugClearBreakpoints)
                s.cb.debugClearBreakpoints();
            auto &r = view.cpu;
            ImGui::Text("AF %02X%02X  BC %02X%02X  DE %02X%02X  HL %02X%02X", r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l);
            ImGui::Text("IX %04X  IY %04X  SP %04X  PC %04X", r.ix, r.iy, r.sp, r.pc);
            ImGui::Text("I %02X  R %02X  IM %u  IFF %u/%u", r.i, r.r, r.im, r.iff1, r.iff2);
            ImGui::Text("Flags: %c%c%c%c%c%c%c%c  Cycles %llu", r.f & 128 ? 'S' : '-', r.f & 64 ? 'Z' : '-', r.f & 32 ? 'Y' : '-', r.f & 16 ? 'H' : '-', r.f & 8 ? 'X' : '-', r.f & 4 ? 'P' : '-', r.f & 2 ? 'N' : '-', r.f & 1 ? 'C' : '-', static_cast<unsigned long long>(r.cycles));
            ImGui::Separator();
            ImGui::TextWrapped("Click an instruction to toggle its breakpoint. Listing starts at PC.");
            ImGui::BeginChild("instructions", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            auto pc = r.pc;
            for (int i = 0; i < 80; ++i)
            {
                auto d = Core::disassemble(pc, s.cb.debugRead);
                bool bp = s.cb.debugHasBreakpoint && s.cb.debugHasBreakpoint(pc);
                char line[256];
                std::snprintf(line, sizeof line, "%s %s %04X  %-12s %s", i == 0 ? ">" : " ", bp ? "*" : " ", pc, d.bytes.c_str(), d.text.c_str());
                ImGui::PushID(i);
                if (ImGui::Selectable(line, false) && s.cb.debugBreakpoint)
                    s.cb.debugBreakpoint(pc);
                ImGui::PopID();
                pc = d.next;
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::Render();
        const auto &io = ImGui::GetIO();
        SDL_SetRenderScale(s.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(s.renderer, 25, 25, 25, 255);
        SDL_RenderClear(s.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s.renderer);
        SDL_RenderPresent(s.renderer);
        ImGui::SetCurrentContext(previous);
        if (!open)
            close();
    }
}