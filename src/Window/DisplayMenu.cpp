#include "DisplayMenu.h"
#include "FullscreenMenu.h"
#include <imgui.h>
#include <string>
#include <utility>

namespace R2SEGA8::Window
{
    void drawDisplayMenu(DisplayMenuContext context)
    {
        if (ImGui::BeginMenu("Display"))
        {
            const bool configurable = context.callbacks.executionSettings && context.callbacks.setExecutionSettings;
            auto settings = configurable ? context.callbacks.executionSettings() : Common::ExecutionSettings{};
            if (ImGui::BeginMenu("Console Region", configurable &&
                                                       (!context.callbacks.consoleProfileAvailable || context.callbacks.consoleProfileAvailable())))
            {
                for (const auto &option : {std::pair<const char *, Common::ConsoleProfile>{"Auto", Common::ConsoleProfile::Auto},
                                           {"NTSC", Common::ConsoleProfile::NTSC},
                                           {"PAL", Common::ConsoleProfile::PAL}})
                    if (ImGui::MenuItem(option.first, nullptr, settings.consoleProfile == option.second))
                    {
                        settings.consoleProfile = option.second;
                        context.callbacks.setExecutionSettings(settings);
                    }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("VSync", nullptr, &settings.vsync, configurable))
                context.callbacks.setExecutionSettings(settings);
            ImGui::Separator();
            for (unsigned scale = 1; scale <= 5; ++scale)
            {
                char label[] = "1X", shortcut[] = "Ctrl+1";
                label[0] = static_cast<char>('0' + scale);
                shortcut[5] = static_cast<char>('0' + scale);
                if (ImGui::MenuItem(label, shortcut, context.scale == scale))
                    context.pendingScale = scale;
            }
            ImGui::Separator();
            R2Window::drawFullscreenResolutionMenu(context.resolution, context.window, "FullScreen Resolution");
            if (ImGui::MenuItem("Fullscreen Stretched"))
                context.pendingFullscreen = FullscreenMode::Stretched;
            if (ImGui::MenuItem("Fullscreen", "Ctrl+F ou Alt+Enter"))
                context.pendingFullscreen = FullscreenMode::Aspect;
            ImGui::Separator();
            if (ImGui::MenuItem("Bilinear Filter", nullptr, &settings.bilinearFilterEnabled, configurable))
            {
                if (settings.bilinearFilterEnabled)
                    settings.pixelArtEnabled = false;
                context.callbacks.setExecutionSettings(settings);
            }
            if (ImGui::MenuItem("Pixel Art", nullptr, &settings.pixelArtEnabled, configurable))
            {
                if (settings.pixelArtEnabled)
                    settings.bilinearFilterEnabled = false;
                context.callbacks.setExecutionSettings(settings);
            }
            if (ImGui::MenuItem("Scanlines", nullptr, &settings.scanlinesEnabled, configurable))
                context.callbacks.setExecutionSettings(settings);
            if (ImGui::BeginMenu("Scanline Levels", configurable && settings.scanlinesEnabled))
            {
                for (unsigned level = 5; level <= 25; level += 5)
                {
                    const auto label = std::to_string(level) + "%";
                    if (ImGui::MenuItem(label.c_str(), nullptr, settings.scanlineLevel == level))
                    {
                        settings.scanlineLevel = level;
                        context.callbacks.setExecutionSettings(settings);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Crop 8px from Left", nullptr, &settings.cropLeftEnabled, configurable))
            {
                context.callbacks.setExecutionSettings(settings);
                context.pendingScale = context.scale;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Render sprites", nullptr, &settings.renderSprites, configurable))
                context.callbacks.setExecutionSettings(settings);
            if (ImGui::MenuItem("Render tiles", nullptr, &settings.renderTiles, configurable))
                context.callbacks.setExecutionSettings(settings);
            ImGui::Separator();
#ifdef _WIN32
            if (ImGui::BeginMenu("Shaders"))
            {
                for (const auto &option : {std::pair<DisplayShader, const char *>{DisplayShader::None, "None"},
                                           {DisplayShader::Gauss, "Gauss Scanlines"},
                                           {DisplayShader::HQ2, "HQ2"},
                                           {DisplayShader::CRT, "CRT"},
                                           {DisplayShader::CRT3D, "CRT 3D"},
                                           {DisplayShader::ScaleFX, "ScaleFX"}})
                    if (ImGui::MenuItem(option.second, nullptr, context.shader == option.first))
                        context.shader = option.first;
                ImGui::EndMenu();
            }
#endif
            ImGui::EndMenu();
        }
    }
}