#pragma once
#include "Systems/KeyboardMapping.h"
#include <functional>

namespace R2Keyboard
{
    // Mantém o rascunho da edição e desenha o diálogo no contexto ImGui atual.
    class KeyboardDialog
    {
    public:
        // Recebe o rascunho; false mantém o diálogo aberto para nova tentativa.
        using Saver = std::function<bool(const Mapping &)>;
        // Define o mapeamento confirmado que será usado na próxima abertura.
        void setMapping(const Mapping &mapping);
        // Retorna o mapeamento carregado ou confirmado mais recentemente.
        const Mapping &mapping() const;
        // Inicia a edição; order relaciona cada linha exibida ao índice da associação.
        void open(const std::array<int, 10> &order);
        // Considera tanto a abertura solicitada quanto o diálogo já apresentado.
        bool active() const;
        // Captura pressões da janela indicada e retorna se o evento foi consumido.
        bool processEvent(const SDL_Event &event, SDL_WindowID id);
        // Desenha o editor; Cancel descarta o rascunho e Save chama o callback.
        void draw(const Saver &save);

    private:
        // A cópia confirmada só é substituída depois de um salvamento bem-sucedido.
        Mapping mapping_ = defaults(), draft_ = mapping_;
        std::array<int, 10> order_{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
        // capture_ identifica a função aguardando uma tecla; -1 indica captura inativa.
        int pad_ = 0, capture_ = -1;
        bool requested_ = false, open_ = false, error_ = false;
    };
}