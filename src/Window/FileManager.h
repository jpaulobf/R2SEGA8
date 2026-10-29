#pragma once

#include "Callbacks.h"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    // Gerencia o seletor de ROMs, a navegação por pastas e os dados de apresentação.
    // Solicita inspeção e carregamento pelos callbacks recebidos.
    class FileManager
    {
    public:
        // Define os callbacks de inspeção e carregamento e usa a pasta atual como ponto inicial.
        FileManager(Callbacks::InspectRom inspectHandler, Callbacks::OpenRom openHandler);

        // Agenda a abertura do seletor e descarta a prévia anterior. Quando recebe
        // um arquivo, tenta abrir sua pasta e preparar a prévia; falhas ficam no diálogo.
        void requestOpen(const std::filesystem::path &file = {});

        // Desenha o diálogo no contexto ImGui atual e processa navegação e carregamento.
        void draw();

    private:
        // Guarda o caminho, o nome de exibição e o tipo de uma entrada da pasta.
        struct Entry
        {
            std::filesystem::path path;
            std::string name;
            bool directory;
        };

        // Lista e ordena a pasta solicitada, atualizando o caminho e limpando a prévia.
        // Falhas de acesso são propagadas para apresentação pelo chamador.
        void navigate(const std::filesystem::path &path);

        // Descarta a prévia anterior e solicita dados de apresentação ao callback.
        // Propaga falhas para apresentação pelo chamador.
        void inspect(const std::filesystem::path &path);

        // Interpreta o campo de caminho como pasta ou arquivo e exibe eventuais erros.
        void openPath();

        // Desenha o seletor de unidades no Windows e navega para a raiz escolhida.
        // Em outras plataformas, não desenha controles.
        void drawDriveSelector();

        Callbacks::InspectRom inspectHandler_;
        Callbacks::OpenRom openHandler_;
        std::filesystem::path directory_;
        std::vector<Entry> entries_;
        std::optional<RomPreview> candidate_;
        std::array<char, 4096> pathInput_{};
        Common::SystemType selected_ = Common::SystemType::Unknown;
        std::string error_;
        bool requested_ = false;
    };
}