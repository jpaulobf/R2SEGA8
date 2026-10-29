#pragma once

#include "Core/Cartridge/Cartridge.h"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    // Gerencia o seletor de ROMs em Dear ImGui, a navegação por pastas e a prévia
    // do cartucho. Delega a criação da máquina à função de carregamento recebida.
    class FileManager
    {
    public:
        // Função chamada com uma cópia do cartucho e o sistema escolhido.
        // Exceções são apresentadas no seletor, mantendo a seleção para nova tentativa.
        using OpenHandler = std::function<void(Core::Cartridge, Common::SystemType)>;

        // Define a função de carregamento e usa o diretório atual como pasta inicial.
        explicit FileManager(OpenHandler handler);

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

        // Descarta a prévia anterior, lê o arquivo e prepara a sugestão de sistema.
        // Propaga falhas de leitura para apresentação pelo chamador.
        void inspect(const std::filesystem::path &path);

        // Interpreta o campo de caminho como pasta ou arquivo e exibe eventuais erros.
        void openPath();

        // Desenha o seletor de unidades no Windows e navega para a raiz escolhida.
        // Em outras plataformas, não desenha controles.
        void drawDriveSelector();
        
        OpenHandler handler_;
        std::filesystem::path directory_;
        std::vector<Entry> entries_;
        std::optional<Core::Cartridge> candidate_;
        std::array<char, 4096> pathInput_{};
        Common::SystemType selected_ = Common::SystemType::Unknown;
        std::string error_;
        bool requested_ = false;
    };
}