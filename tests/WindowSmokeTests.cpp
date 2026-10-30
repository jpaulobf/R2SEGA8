#include "Window/Window.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <fstream>
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
                if (SDL_GetTextureSize(texture, &w, &h) && w == width && h == height &&
                    SDL_GetTextureScaleMode(texture, &filter) && filter == SDL_SCALEMODE_NEAREST)
                    return true;
            }
        return false;
    }
}

int main()
{
    std::filesystem::path fixture;
    try
    {
        fixture = std::filesystem::temp_directory_path() /
                  ("r2sega8-window-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sg");
        {
            std::ofstream file(fixture, std::ios::binary);
            file << "test ROM";
        }
        // Duas sessões verificam o encerramento e a recriação dos recursos SDL3 e ImGui.
        for (int session = 0; session < 2; ++session)
        {
            int loads = 0;
            int exits = 0, inspections = 0, memoryQueries = 0;
            bool rejectLoad = false;
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
            callbacks.inspectRom = [&](const std::filesystem::path &path)
            {
                ++inspections;
                const auto size = std::filesystem::file_size(path);
                return R2SEGA8::Window::RomPreview{path, static_cast<std::size_t>(size), R2SEGA8::Common::SystemType::SG1000};
            };
            callbacks.openRom = [&](const std::filesystem::path &path, auto type)
            {
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
            R2SEGA8::Window::Window window(std::move(callbacks));
            window.open();
            if (!window.processEvents())
                throw std::runtime_error("Window closed unexpectedly");
            window.render();
            window.render();
            window.requestOpen(fixture);
            window.render();
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Open ROM dialog did not open");
            // Usa a fila de ativação do ImGui para acionar o próprio botão Load ROM.
            // Dispensa diálogos nativos e suposições sobre coordenadas de tela.
            const auto *dialog = ImGui::FindWindowByName("Open ROM");
            if (!dialog)
                throw std::runtime_error("ROM chooser is missing");
            ImGui::ActivateItemByID(ImHashStr("Load ROM", 0, dialog->ID));
            window.render();
            if (loads != 1 || loadedPath != fixture || loadedType != R2SEGA8::Common::SystemType::SG1000 || !memoryQueries || inspections != 1)
                throw std::runtime_error("Window did not route preview, load and presentation through callbacks");
            window.render();
            if (!hasVideoTexture(8, 8))
                throw std::runtime_error("Frame callback did not produce a nearest-filtered presentation texture");
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
            rejectLoad = true;
            window.requestOpen(fixture);
            window.render();
            ImGui::ActivateItemByID(ImHashStr("Load ROM", 0, dialog->ID));
            window.render();
            if (loads != 1 || loadedPath != fixture || !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Callback error closed the dialog or replaced the active selection");
            window.requestOpen(fixture.parent_path() / "missing.sg");
            window.render(); // O erro deve permanecer no diálogo, sem encerrar a aplicação.
            ImGui::ActivateItemByID(ImHashStr("Load ROM", 0, dialog->ID));
            window.render();
            if (loads != 1)
                throw std::runtime_error("Invalid selection loaded a stale cartridge");
            if (inspections != 3)
                throw std::runtime_error("Invalid selection did not go through the inspection callback");
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&quit) || !SDL_PushEvent(&quit) || window.processEvents() || exits != 1)
                throw std::runtime_error("Quit event was ignored");
        }
        std::filesystem::remove(fixture);
        std::cout << "Window, ROM inspector, file dialog, error handling and shutdown passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::error_code ignored;
        if (!fixture.empty())
            std::filesystem::remove(fixture, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}