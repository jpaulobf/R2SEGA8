#pragma once

#include "Callbacks.h"
#include <memory>

namespace R2SEGA8::Window
{
    // Geometria nativa de apresentação, fornecida pela aplicação sem expor o núcleo à interface.
    struct DisplaySize
    {
        unsigned width, height;
    };

    // Gerencia a janela SDL3, os eventos e a interface Dear ImGui.
    // Mantém os recursos nativos privados e os libera na ordem inversa da inicialização.
    class Window
    {
    public:
        // Configura callbacks e geometria nativa; a escala inicial é 3X, sem criar recursos SDL3 ou ImGui.
        explicit Window(Callbacks callbacks, DisplaySize nativeSize);

        // Encerra os componentes ImGui e libera o renderizador, a janela e a sessão SDL3.
        ~Window();

        // Impede copiar uma sessão que possui recursos nativos exclusivos.
        Window(const Window &) = delete;

        // Impede substituir ou compartilhar os recursos da sessão por atribuição.
        Window &operator=(const Window &) = delete;

        // Inicializa SDL3, a janela, o renderizador e o ImGui. Lança uma exceção
        // em caso de falha ou se a sessão SDL3 já tiver sido inicializada.
        void open();

        // Agenda o seletor; um caminho de ROM solicita carregamento imediato no próximo desenho.
        void requestOpen(const std::filesystem::path &path = {});

        // Encaminha eventos ao ImGui e aos callbacks, informando a captura; trata saída, Ctrl+O e arquivos arrastados.
        // Retorna false quando a saída foi solicitada; exige uma sessão inicializada.
        bool processEvents();

        // Consulta a apresentação e notifica ações e disponibilidade de entrada pelos callbacks.
        // Exige uma sessão inicializada e propaga falhas de renderização SDL3.
        void render();

    private:
        // Oculta os tipos nativos e centraliza o estado e a liberação dos recursos.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}