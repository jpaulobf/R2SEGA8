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
            };
            callbacks.memory = [&]() -> R2SEGA8::Window::MemoryView
            {
                ++memoryQueries;
                return {{rom.data(), rom.size()}, {ram.data(), ram.size()}};
            };
            callbacks.exit = [&]()
            { ++exits; };
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