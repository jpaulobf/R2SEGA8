#include "Window/Window.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    // Confirma que um quadro neutro chegou ao backend como textura com filtro de pixels.
    bool hasVideoTexture(unsigned width, unsigned height)
    {
        const auto *data = ImGui::GetDrawData();
        for (const auto *list : data->CmdLists)
            for (const auto &command : list->CmdBuffer)
            {
                if (command.UserCallback || command.GetTexID() == ImTextureID_Invalid)
                    continue;
                auto *texture = reinterpret_cast<SDL_Texture *>(command.GetTexID());
                float w = 0, h = 0;
                SDL_ScaleMode filter;
                if (SDL_GetTextureSize(texture, &w, &h) && w == width && h == height && SDL_GetTextureScaleMode(texture, &filter) &&
                    filter == SDL_SCALEMODE_NEAREST)
                    return true;
            }
        return false;
    }
    // Obtém o retângulo efetivamente enviado ao renderizador, incluindo centralização e escala.
    ImRect videoBounds(unsigned width, unsigned height)
    {
        const auto *data = ImGui::GetDrawData();
        for (const auto *list : data->CmdLists)
            for (const auto &command : list->CmdBuffer)
            {
                if (command.UserCallback || command.GetTexID() == ImTextureID_Invalid)
                    continue;
                auto *texture = reinterpret_cast<SDL_Texture *>(command.GetTexID());
                float w = 0, h = 0;
                if (!SDL_GetTextureSize(texture, &w, &h) || w != width || h != height)
                    continue;
                ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
                for (unsigned i = 0; i < command.ElemCount; ++i)
                    bounds.Add(list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i] + command.VtxOffset].pos);
                return bounds;
            }
        throw std::runtime_error("Video rectangle is missing");
    }

    // Confere o tamanho físico do vídeo e o espaço adicional reservado à barra de menus.
    void checkScale(SDL_Window *native, unsigned scale)
    {
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(native, &width, &height))
            throw std::runtime_error(SDL_GetError());
        const auto bounds = videoBounds(8, 8);
        const auto density = SDL_GetWindowPixelDensity(native);
        if (width != static_cast<int>(256 * scale) || std::abs(height - (192 * scale + ImGui::GetFrameHeight() * density)) > 1 ||
            std::abs(bounds.GetWidth() * density - 256 * scale) > 1 || std::abs(bounds.GetHeight() * density - 192 * scale) > 1)
            throw std::runtime_error("Display scale mismatch: " + std::to_string(scale) + "X window=" + std::to_string(width) + "x" +
                                     std::to_string(height) + " video=" + std::to_string(bounds.GetWidth()) + "x" +
                                     std::to_string(bounds.GetHeight()) + " menu=" + std::to_string(ImGui::GetFrameHeight()));
    }

    // Usa atalhos e o próprio menu, e simula as transições que o backend dummy não maximiza nativamente.
    void displayOptions(R2SEGA8::Window::Window &window, SDL_Window *native)
    {
        checkScale(native, 3);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = SDL_GetWindowID(native);
        key.key.mod = SDL_KMOD_CTRL;
        for (unsigned scale = 1; scale <= 5; ++scale)
        {
            key.key.key = SDLK_1 + scale - 1;
            SDL_PushEvent(&key);
            window.processEvents();
            window.render();
            window.render();
            checkScale(native, scale);
        }
        key.key.key = SDLK_1;
        key.key.windowID += 1000;
        SDL_PushEvent(&key);
        window.processEvents();
        checkScale(native, 5);
        key.key.windowID = SDL_GetWindowID(native);
        key.key.repeat = true;
        SDL_PushEvent(&key);
        window.processEvents();
        checkScale(native, 5);
        key.key.repeat = false;

        // Um tamanho normal arbitrário volta ao preset sem limitar o tamanho maximizado.
        SDL_SetWindowSize(native, 900, 500);
        SDL_Event resized{};
        resized.type = SDL_EVENT_WINDOW_RESIZED;
        resized.window.windowID = SDL_GetWindowID(native);
        SDL_PushEvent(&resized);
        window.processEvents();
        window.render();
        window.render();
        checkScale(native, 5);

        const auto *main = ImGui::FindWindowByName("R2SEGA8");
        const auto menuBar = ImHashStr("##MenuBar", 0, main->ID);
        ImGui::ActivateItemByID(ImHashStr("Display", 0, menuBar));
        window.render();
        const auto *menu = ImGui::FindWindowByName("###Menu_00");
        if (!menu || !menu->Active)
            throw std::runtime_error("Display menu did not open");
        ImGui::ActivateItemByID(ImHashStr("2X", 0, menu->ID));
        window.render();
        window.processEvents();
        window.render();
        checkScale(native, 2);

        SDL_Event maximized{};
        maximized.type = SDL_EVENT_WINDOW_MAXIMIZED;
        maximized.window.windowID = SDL_GetWindowID(native);
        SDL_PushEvent(&maximized);
        for (const auto size : {ImVec2(1600, 900), ImVec2(900, 1200)})
        {
            SDL_SetWindowSize(native, static_cast<int>(size.x), static_cast<int>(size.y));
            window.processEvents();
            window.render();
            window.render();
            const auto bounds = videoBounds(8, 8);
            const float availableHeight = size.y - ImGui::GetFrameHeight();
            const float scale = std::min(size.x / 256.0f, availableHeight / 192.0f);
            if (std::abs(bounds.GetWidth() - 256 * scale) > 1 || std::abs(bounds.GetHeight() - 192 * scale) > 1 ||
                std::abs(bounds.Min.x - (size.x - bounds.GetWidth()) * 0.5f) > 1 ||
                std::abs(bounds.Min.y - (ImGui::GetFrameHeight() + (availableHeight - bounds.GetHeight()) * 0.5f)) > 1)
                throw std::runtime_error("Maximized video must fill the available area and preserve centered 4:3 aspect");
        }
        maximized.type = SDL_EVENT_WINDOW_RESTORED;
        SDL_PushEvent(&maximized);
        window.processEvents();
        window.render();
        window.render();
        checkScale(native, 2);
        key.key.key = SDLK_3;
        SDL_PushEvent(&key);
        window.processEvents();
        window.render();
        window.render();
        checkScale(native, 3);
    }
    // Aciona a linha real do seletor pela sua ID, sem coordenadas de tela ou uma confirmação intermediária.
    void activateROMEntry(R2SEGA8::Window::Window &window, int index)
    {
        const auto *dialog = ImGui::FindWindowByName("Open ROM");
        const ImGuiWindow *files = nullptr;
        if (dialog)
            for (const auto *child : dialog->DC.ChildWindows)
                if (std::string(child->Name).find("/Files_") != std::string::npos)
                    files = child;
        if (!files)
            throw std::runtime_error("ROM file list is missing");
        const auto row = ImHashData(&index, sizeof(index), files->ID);
        ImGui::ActivateItemByID(ImHashStr("##entry", 0, row));
        window.render();
    }

    // Remove somente os arquivos e a pasta que esta suíte criou, sem apagar conteúdo recursivamente.
    void cleanupROMFixtures(const std::filesystem::path &fixture)
    {
        if (fixture.empty())
            return;
        std::error_code ignored;
        for (const auto name : {"01.sg", "02.Sc", "03.SMS", "04.GG", "05.BIN", "06.ZIP", "07##.SG"})
            std::filesystem::remove(fixture.parent_path() / name, ignored);
        std::filesystem::remove(fixture.parent_path(), ignored);
    }
} // namespace

int main()
{
    std::filesystem::path fixture;
    try
    {
        const auto directory = std::filesystem::temp_directory_path() /
                               ("r2sega8-window-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        fixture = directory / "01.sg";
        for (const auto name : {"01.sg", "02.Sc", "03.SMS", "04.GG", "05.BIN", "06.ZIP", "07##.SG"})
        {
            std::ofstream file(directory / name, std::ios::binary);
            file << "test ROM";
        }
        // Duas sessões verificam o encerramento e a recriação dos recursos SDL3 e ImGui.
        for (int session = 0; session < 2; ++session)
        {
            int loads = 0;
            int exits = 0, attempts = 0, memoryQueries = 0;
            bool rejectLoad = false;
            bool inputAllowed = false, lastKeyAllowed = false;
            unsigned keyEvents = 0;
            bool frameEnabled = false;
            unsigned frameWidth = 8, frameHeight = 8;
            std::array<std::uint8_t, 8 * 8 * 4> pixels{};
            for (std::size_t i = 0; i < pixels.size(); i += 4)
            {
                pixels[i] = 33;
                pixels[i + 1] = 200;
                pixels[i + 2] = 66;
                pixels[i + 3] = 255;
            }
            std::filesystem::path loadedPath;
            auto loadedType = R2SEGA8::Common::SystemType::Unknown;
            std::vector<std::uint8_t> rom;
            std::array<std::uint8_t, 1024> ram{};
            R2SEGA8::Window::Callbacks callbacks;
            // O teste da interface usa dados neutros e não vincula o núcleo de emulação.
            callbacks.inputEvent = [&](const SDL_Event &event, bool enabled)
            {
                if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                {
                    ++keyEvents;
                    lastKeyAllowed = enabled;
                }
            };
            callbacks.inputAvailability = [&](bool enabled)
            { inputAllowed = enabled; };
            callbacks.openRom = [&](const std::filesystem::path &path, auto type)
            {
                ++attempts;
                if (rejectLoad)
                    throw std::runtime_error("Load rejected by application callback");
                loadedPath = path;
                loadedType = type;
                rom.assign(8, 0x42);
                ++loads;
                frameEnabled = true;
            };
            callbacks.memory = [&]() -> R2SEGA8::Window::MemoryView
            {
                ++memoryQueries;
                return {{rom.data(), rom.size()}, {ram.data(), ram.size()}};
            };
            callbacks.exit = [&]()
            { ++exits; };
            callbacks.frame = [&]() -> R2SEGA8::Window::FrameView
            {
                if (!frameEnabled)
                    return {};
                return {pixels.data(), frameWidth, frameHeight, frameWidth * 4};
            };
            R2SEGA8::Window::Window window(std::move(callbacks), {256, 192});
            window.open();
            if (!window.processEvents())
                throw std::runtime_error("Window closed unexpectedly");
            window.render();
            window.render();
            int nativeCount = 0;
            auto **nativeWindows = SDL_GetWindows(&nativeCount);
            if (!nativeWindows || nativeCount != 1)
                throw std::runtime_error("Missing native test window");
            auto *nativeWindow = nativeWindows[0];
            const auto windowID = SDL_GetWindowID(nativeWindow);
            SDL_free(nativeWindows);
            SDL_Event focus{};
            focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            focus.window.windowID = windowID;
            SDL_PushEvent(&focus);
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID = windowID;
            key.key.key = SDLK_K;
            SDL_PushEvent(&key);
            window.processEvents();
            if (!inputAllowed || !keyEvents || !lastKeyAllowed)
                throw std::runtime_error("Focused gameplay input was captured by idle navigation");
            focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            focus.window.windowID = windowID + 1000;
            SDL_PushEvent(&focus);
            window.processEvents();
            if (!inputAllowed)
                throw std::runtime_error("Foreign window focus event disabled gameplay");
            focus.window.windowID = windowID;
            SDL_PushEvent(&focus);
            key.type = SDL_EVENT_KEY_UP;
            const auto beforeRelease = keyEvents;
            SDL_PushEvent(&key);
            window.processEvents();
            if (inputAllowed || lastKeyAllowed || keyEvents != beforeRelease + 1)
                throw std::runtime_error("Focus loss must disable input while still routing releases");
            focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            SDL_PushEvent(&focus);
            window.processEvents();
            ImGui::GetIO().WantCaptureKeyboard = true;
            SDL_PushEvent(&key);
            window.processEvents();
            if (inputAllowed || lastKeyAllowed)
                throw std::runtime_error("ImGui capture did not block gameplay");
            ImGui::GetIO().WantCaptureKeyboard = false;
            window.requestOpen(fixture.parent_path());
            SDL_PushEvent(&key);
            window.processEvents();
            if (inputAllowed || lastKeyAllowed)
                throw std::runtime_error("Requested dialog must block input before its first draw");
            window.render();
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Open ROM dialog did not open");
            if (inputAllowed)
                throw std::runtime_error("Open ROM dialog must keep gameplay input disabled");
            activateROMEntry(window, 0);
            if (loads != 1 || attempts != 1 || loadedPath != fixture || loadedType != R2SEGA8::Common::SystemType::SG1000 ||
                memoryQueries != 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Clicking a ROM must load once by extension and close the dialog");
            window.render();
            if (!hasVideoTexture(8, 8))
                throw std::runtime_error("Frame callback did not produce a nearest-filtered presentation texture");
            displayOptions(window, nativeWindow);
            frameWidth = 16;
            frameHeight = 4;
            window.render();
            if (!hasVideoTexture(16, 4))
                throw std::runtime_error("Frame dimension change did not recreate presentation texture");
            frameEnabled = false;
            window.render();
            if (hasVideoTexture(16, 4))
                throw std::runtime_error("Empty frame callback displayed a stale video image");
            frameEnabled = true;
            const R2SEGA8::Common::SystemType systems[] = {R2SEGA8::Common::SystemType::SC3000, R2SEGA8::Common::SystemType::MasterSystem,
                                                           R2SEGA8::Common::SystemType::GameGear};
            for (int index = 1; index <= 3; ++index)
            {
                window.requestOpen(fixture.parent_path());
                window.render();
                activateROMEntry(window, index);
                if (loads != index + 1 || loadedType != systems[index - 1])
                    throw std::runtime_error("ROM extension must select the system case-insensitively");
            }
            const auto previousPath = loadedPath;
            rejectLoad = true;
            window.requestOpen(fixture.parent_path());
            window.render();
            activateROMEntry(window, 0);
            const auto failedAttempts = attempts;
            window.render();
            window.render();
            if (loads != 4 || attempts != failedAttempts || loadedPath != previousPath ||
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("A rejected click must preserve the active ROM and must not retry every frame");
            rejectLoad = false;
            activateROMEntry(window, 6);
            if (loads != 5 || loadedPath.filename() != "07##.SG" || loadedType != R2SEGA8::Common::SystemType::SG1000)
                throw std::runtime_error("A fresh click must recover from errors and preserve filenames containing ##");
            for (int index : {4, 5})
            {
                window.requestOpen(fixture.parent_path());
                window.render();
                const auto before = attempts;
                activateROMEntry(window, index);
                if (loads != 5 || attempts != before ||
                    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    throw std::runtime_error("BIN/ZIP must report pending support without guessing a system or loading a stale ROM");
            }
            const auto beforeMissing = attempts;
            window.requestOpen(fixture);
            window.requestOpen(fixture.parent_path() / "missing.sg");
            window.render();
            window.render();
            if (loads != 5 || attempts != beforeMissing)
                throw std::runtime_error("Missing file must discard an older scheduled load");
            window.requestOpen(fixture);
            window.render();
            if (loads != 6 || loadedPath != fixture ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Direct file requests must also load without confirmation");
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&quit) || !SDL_PushEvent(&quit) || window.processEvents() || exits != 1)
                throw std::runtime_error("Quit event was ignored");
        }
        cleanupROMFixtures(fixture);
        std::cout << "Window, display, direct ROM selection, extensions, file errors, input and shutdown passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::error_code ignored;
        cleanupROMFixtures(fixture);
        std::cerr << error.what() << '\n';
        return 1;
    }
}