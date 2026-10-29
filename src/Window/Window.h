#pragma once

#include "FileManager.h"
#include <memory>

namespace R2SEGA8::Systems
{
    class System;
}

namespace R2SEGA8::Window
{
    // Gerencia a janela SDL3, os eventos e a interface Dear ImGui.
    // Mantém os recursos nativos privados e os libera na ordem inversa da inicialização.
    class Window
    {
    public:
        // Configura o carregamento de ROMs sem criar recursos SDL3 ou ImGui.
        explicit Window(FileManager::OpenHandler openHandler);

        // Encerra os componentes ImGui e libera o renderizador, a janela e a sessão SDL3.
        ~Window();

        // Impede copiar uma sessão que possui recursos nativos exclusivos.
        Window(const Window &) = delete;

        // Impede substituir ou compartilhar os recursos da sessão por atribuição.
        Window &operator=(const Window &) = delete;

        // Inicializa SDL3, a janela, o renderizador e o ImGui. Lança uma exceção
        // em caso de falha ou se a sessão SDL3 já tiver sido inicializada.
        void open();

        // Agenda o seletor de ROMs, preparando a prévia quando um caminho é informado.
        void requestOpen(const std::filesystem::path &path = {});

        // Encaminha eventos ao ImGui e trata saída, Ctrl+O e arquivos arrastados.
        // Retorna false quando a saída foi solicitada; exige uma sessão inicializada.
        bool processEvents();
        
        // Desenha e apresenta a interface; system pode ser nulo quando não há ROM.
        // A função de carregamento pode substituir a máquina após sua exibição.
        // Exige uma sessão inicializada e propaga falhas de renderização SDL3.
        void render(const Systems::System *system);

    private:
        // Oculta os tipos nativos e centraliza o estado e a liberação dos recursos.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
