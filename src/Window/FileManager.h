#pragma once

#include "Callbacks.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    // Gerencia o seletor de ROMs, a navegação por pastas e os dados de apresentação.
    // Solicita carregamento por callback ao clicar em uma ROM com extensão conhecida.
    class FileManager
    {
    public:
        // Define o callback de carregamento e usa a pasta atual como ponto inicial.
        explicit FileManager(Callbacks::OpenRom openHandler);

        // Agenda o seletor e descarta pedidos anteriores. Um arquivo será carregado no próximo desenho;
        // uma pasta será aberta para navegação. Falhas ficam no diálogo.
        void requestOpen(const std::filesystem::path &file = {});

        // Desenha o diálogo no contexto ImGui atual e processa navegação e carregamento.
        void draw();

        // Indica um diálogo aberto ou solicitado para bloquear comandos de jogo antes de desenhá-lo.
        bool isOpen() const { return requested_ || open_; }

        const std::vector<std::filesystem::path> &recentFiles() const { return recentFiles_; }

    private:
        // Guarda o caminho, o nome de exibição e o tipo de uma entrada da pasta.
        struct Entry
        {
            std::filesystem::path path;
            std::string name;
            bool directory;
        };

        // Lista e ordena a pasta solicitada, atualizando a label do caminho.
        // Falhas de acesso são propagadas para apresentação pelo chamador.
        void navigate(const std::filesystem::path &path);

        // Valida extensão/arquivo, solicita carga imediata e fecha o diálogo somente após sucesso.
        // Propaga falhas ao chamador, sem reter um candidato antigo para nova carga.
        void load(const std::filesystem::path &path);

        // Desenha o seletor de unidades no Windows e navega para a raiz escolhida.
        // Em outras plataformas, não desenha controles.
        void drawDriveSelector();

        void readConfig();
        void recordRecent(const std::filesystem::path &path);

        Callbacks::OpenRom openHandler_;
        std::filesystem::path directory_;
        std::vector<Entry> entries_;
        std::vector<std::filesystem::path> recentFiles_;
        std::filesystem::path lastRomDirectory_;
        std::optional<std::filesystem::path> pendingFile_;
        std::string pathLabel_;
        std::string error_;
        bool requested_ = false, open_ = false;
    };
}