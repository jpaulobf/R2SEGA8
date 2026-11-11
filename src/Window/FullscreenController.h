#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <cmath>

// Controla a resolução da sessão e as transições SDL. A interface desenha o menu.
class FullscreenController
{
    struct Resolution
    {
        int width, height;
        const char *label;
    };
    inline static constexpr std::array<Resolution, 12> resolutions{{{640, 480, "640 x 480"}, {800, 600, "800 x 600"}, {832, 624, "832 x 624"}, {1024, 768, "1024 x 768"}, {1128, 634, "1128 x 634"}, {1280, 720, "1280 x 720"}, {1280, 960, "1280 x 960"}, {1366, 768, "1366 x 768"}, {1440, 900, "1440 x 900"}, {1600, 900, "1600 x 900"}, {1680, 1050, "1680 x 1050"}, {1920, 1080, "1920 x 1080"}}};
    int selected_ = -1;
    std::array<bool, 12> unavailable_{};

    bool findMode(SDL_Window *window, int index, SDL_DisplayMode &result) const
    {
        const auto display = SDL_GetDisplayForWindow(window);
        const auto *desktop = SDL_GetDesktopDisplayMode(display);
        if (!desktop || desktop->refresh_rate <= 0)
            return false;
        int count = 0;
        auto modes = SDL_GetFullscreenDisplayModes(display, &count);
        bool found = false;
        for (int i = 0; i < count; ++i)
            if (modes[i]->w == resolutions[index].width && modes[i]->h == resolutions[index].height &&
                std::abs(modes[i]->refresh_rate - desktop->refresh_rate) < 0.01f)
            {
                result = *modes[i];
                found = true;
                break;
            }
        SDL_free(modes);
        return found;
    }

public:
    static constexpr int count() { return static_cast<int>(resolutions.size()); }
    static const char *label(int index) { return resolutions[index].label; }
    int selected() const { return selected_; }
    bool available(int index) const { return !unavailable_[index]; }
    void selectDesktop() { selected_ = -1; }
    void select(SDL_Window *window, int index)
    {
        SDL_DisplayMode mode{};
        if (findMode(window, index, mode))
            selected_ = index;
        else
        {
            unavailable_[index] = true;
            selected_ = -1;
        }
    }
    bool enter(SDL_Window *window)
    {
        SDL_DisplayMode mode{};
        if (selected_ >= 0 && (!findMode(window, selected_, mode) || !SDL_SetWindowFullscreenMode(window, &mode)))
        {
            unavailable_[selected_] = true;
            selected_ = -1;
        }
        if (selected_ < 0 && !SDL_SetWindowFullscreenMode(window, nullptr))
            return false;
        if (!SDL_SetWindowFullscreen(window, true))
        {
            if (selected_ < 0)
                return false;
            unavailable_[selected_] = true;
            selected_ = -1;
            if (!SDL_SetWindowFullscreenMode(window, nullptr) || !SDL_SetWindowFullscreen(window, true))
                return false;
        }
        return SDL_SyncWindow(window);
    }
    bool leave(SDL_Window *window)
    {
        return SDL_SetWindowFullscreen(window, false) && SDL_SyncWindow(window);
    }
};