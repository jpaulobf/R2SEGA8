#include "Window/Window.h"
#include "Systems/Board.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

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
            std::unique_ptr<R2SEGA8::Systems::Board> board;
            int loads = 0;
            R2SEGA8::Window::Window window([&](auto cartridge, auto type)
                                           {
                board = R2SEGA8::Systems::createBoard(type, std::move(cartridge));
                ++loads; });
            window.open();
            if (!window.processEvents())
                throw std::runtime_error("Window closed unexpectedly");
            window.render(nullptr);
            window.render(board.get());
            window.requestOpen(fixture);
            window.render(board.get());
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                throw std::runtime_error("Open ROM dialog did not open");
            // Usa a fila de ativação do ImGui para acionar o próprio botão Load ROM.
            // Dispensa diálogos nativos e suposições sobre coordenadas de tela.
            const auto *dialog = ImGui::FindWindowByName("Open ROM");
            if (!dialog)
                throw std::runtime_error("ROM chooser is missing");
            ImGui::ActivateItemByID(ImHashStr("Load ROM", 0, dialog->ID));
            window.render(board.get());
            if (loads != 1 || !board || board->type() != R2SEGA8::Common::SystemType::SG1000 ||
                board->cartridge().rom().size() != 8)
                throw std::runtime_error("Load ROM button did not populate the active system");
            window.requestOpen(fixture.parent_path() / "missing.sg");
            window.render(board.get()); // O erro deve permanecer no diálogo, sem encerrar a aplicação.
            ImGui::ActivateItemByID(ImHashStr("Load ROM", 0, dialog->ID));
            window.render(board.get());
            if (loads != 1)
                throw std::runtime_error("Invalid selection loaded a stale cartridge");
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            if (!SDL_PushEvent(&quit) || window.processEvents())
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