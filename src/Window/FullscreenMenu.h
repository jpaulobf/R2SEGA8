#pragma once

#include "FullscreenController.h"
#include <imgui.h>

namespace R2Window
{
    inline void drawFullscreenResolutionMenu(FullscreenController &controller, SDL_Window *window, const char *title)
    {
        if (!ImGui::BeginMenu(title))
            return;
        if (ImGui::MenuItem("Desktop Resolution", nullptr, controller.selected() < 0))
            controller.selectDesktop();
        for (int i = 0; i < controller.count(); ++i)
            if (ImGui::MenuItem(controller.label(i), nullptr, controller.selected() == i, controller.available(i)))
                controller.select(window, i);
        ImGui::EndMenu();
    }
}