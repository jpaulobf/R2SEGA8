#include "Window/Window.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        unsigned width = 256, height = 192;
        std::vector<std::uint8_t> rgba(256 * 192 * 4);
        for (std::size_t i = 0; i < rgba.size(); i += 4)
        {
            rgba[i] = 32;
            rgba[i + 1] = 160;
            rgba[i + 2] = 224;
            rgba[i + 3] = 255;
        }
        const auto original = rgba;
        R2SEGA8::Common::ExecutionSettings settings;
        R2SEGA8::Window::Callbacks callbacks;
        callbacks.frame = [&]
        { return R2SEGA8::Window::FrameView{rgba.data(), width, height, width * 4}; };
        callbacks.presentationSize = [&]
        { return std::make_pair(width, height); };
        callbacks.hasRom = []
        { return true; };
        callbacks.executionSettings = [&]
        { return settings; };
        callbacks.setExecutionSettings = [&](const auto &value)
        { settings = value; };
        R2SEGA8::Window::Window window(callbacks, {width, height});
        window.open();
        auto frame = [&]
        { window.processEvents(); window.render(); };
        frame();
        frame();
        auto *native = SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
        require(native != nullptr, "Window is missing");
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2SEGA8");
            ImGui::ActivateItemByID(ImHashStr("Display", 0, ImHashStr("##MenuBar", 0, main->ID)));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("###Menu_00");
            require(menu && menu->Active, "Display menu did not open");
            ImGui::ActivateItemByID(menu->GetID("Shaders"));
            frame();
            frame();
            auto *submenu = ImGui::FindWindowByName("###Menu_01");
            require(submenu && submenu->Active, "Shaders menu did not open");
            ImGui::ActivateItemByID(submenu->GetID(label));
            frame();
            frame();
            frame();
        };
        const auto verify = [&]
        {
            SDL_Texture *game = nullptr;
            for (const auto *list : ImGui::GetDrawData()->CmdLists)
                for (const auto &cmd : list->CmdBuffer)
                {
                    if (cmd.UserCallback || cmd.GetTexID() == ImTextureID_Invalid)
                        continue;
                    auto *tex = reinterpret_cast<SDL_Texture *>(cmd.GetTexID());
                    float w = 0, h = 0;
                    if (SDL_GetTextureSize(tex, &w, &h) && w >= width * 2 && h >= height * 2)
                        game = tex;
                }
            require(game, "Shader output texture missing");
            require(SDL_SetTextureBlendMode(game, SDL_BLENDMODE_BLEND), "Could not enable alpha composition");
            frame();
            auto *surface = SDL_RenderReadPixels(SDL_GetRenderer(native), nullptr);
            require(surface, "Could not read presentation");
            Uint8 r = 0, g = 0, b = 0, a = 0;
            const bool read = SDL_ReadSurfacePixel(surface, surface->w / 2, surface->h / 2, &r, &g, &b, &a);
            SDL_DestroySurface(surface);
            require(read && g > 60 && b > 60 && b > r, "Game image missing, transparent or colors swapped");
        };
        for (const char *label : {"Gauss Scanlines", "HQ2", "CRT", "CRT 3D", "ScaleFX"})
        {
            select(label);
            verify();
        }
        require(rgba == original, "Shaders must not modify the source frame");
        settings.cropLeftEnabled = true;
        frame();
        frame();
        verify();
        settings.cropLeftEnabled = false;
        width = 160;
        height = 144;
        frame();
        frame();
        verify(); // ScaleFX at Game Gear resolution
        select("Gauss Scanlines");
        verify(); // Uses the 160 px NTSC preset.
        select("None");
        std::cout << "Shader menu and display tests passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}