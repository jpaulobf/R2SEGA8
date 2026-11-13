#include "Window/KeyboardDialog.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <cstring>
#include <stdexcept>

static void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
// Exercita os dois controles, a captura, Cancel, Default e a tentativa de salvar novamente.
int main()
{
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto *window = SDL_CreateWindow("keyboard-test", 800, 600, 0);
    check(window != nullptr, SDL_GetError());
    auto *renderer = SDL_CreateRenderer(window, nullptr);
    check(renderer != nullptr, SDL_GetError());
    ImGui::CreateContext();
    check(ImGui_ImplSDL3_InitForSDLRenderer(window, renderer), "ImGui SDL init failed");
    check(ImGui_ImplSDLRenderer3_Init(renderer), "ImGui renderer init failed");
    R2Keyboard::KeyboardDialog dialog;
    auto applied = R2Keyboard::defaults();
    bool allowSave = false;
    int attempts = 0;
    const auto frame = [&]()
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Keyboard test");
        dialog.draw([&](const auto &mapping)
                    {
            ++attempts;
            if (!allowSave) return false;
            applied = mapping;
            return true; });
        ImGui::End();
        ImGui::Render();
    };
    const auto child = [](const char *name)
    {
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(candidate->Name, name))
                return candidate;
        return static_cast<ImGuiWindow *>(nullptr);
    };
    dialog.open({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    frame();
    auto *controllers = child("Configure Keyboard/controllers");
    check(controllers != nullptr, "controllers missing");
    ImGui::ActivateItemByID(controllers->GetID("Pad 2"));
    frame();
    auto *bindings = child("Configure Keyboard/bindings");
    check(bindings != nullptr, "bindings missing");
    ImGui::ActivateItemByID(ImHashStr("###key", 0, bindings->GetID(0)));
    frame();
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.key = SDLK_Q;
    check(dialog.processEvent(event, event.key.windowID), "key not captured");
    frame();
    auto *popup = ImGui::FindWindowByName("Configure Keyboard");
    check(popup != nullptr, "popup missing");
    ImGui::ActivateItemByID(popup->GetID("Save"));
    frame();
    check(dialog.active() && attempts == 1 && applied[1][0] == SDLK_UP, "failed save applied or closed");
    allowSave = true;
    ImGui::ActivateItemByID(popup->GetID("Save"));
    frame();
    check(!dialog.active() && attempts == 2 && applied[1][0] == SDLK_Q && applied[0][0] == SDLK_W,
          "successful save failed to preserve independent pads");
    dialog.open({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    frame();
    popup = ImGui::FindWindowByName("Configure Keyboard");
    ImGui::ActivateItemByID(popup->GetID("Default"));
    frame();
    ImGui::ActivateItemByID(popup->GetID("Cancel"));
    frame();
    check(!dialog.active() && applied[1][0] == SDLK_Q, "cancel must discard the default draft");
    dialog.open({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    frame();
    popup = ImGui::FindWindowByName("Configure Keyboard");
    ImGui::ActivateItemByID(popup->GetID("Default"));
    frame();
    ImGui::ActivateItemByID(popup->GetID("Save"));
    frame();
    check(applied == R2Keyboard::defaults(), "default must restore both pads");
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}