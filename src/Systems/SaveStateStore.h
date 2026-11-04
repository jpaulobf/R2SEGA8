#pragma once

#include "Core/Board.h"
#include <filesystem>
#include <vector>

namespace R2SEGA8::Systems
{
    // Persiste os slots de uma máquina; não altera a placa ativa durante a leitura.
    class SaveStateStore
    {
    public:
        // Define o diretório que conterá os arquivos de estado.
        explicit SaveStateStore(std::filesystem::path directory);

        // Serializa a placa e grava o slot da ROM no diretório configurado.
        void save(const Core::Board &board, unsigned slot) const;
        
        // Lê os bytes do slot da ROM para posterior validação pela placa.
        std::vector<std::uint8_t> load(const Core::Board &board, unsigned slot) const;

    private:
        // Resolve o nome do arquivo a partir do sistema, da ROM e do slot.
        std::filesystem::path path(const Core::Board &board, unsigned slot) const;
        std::filesystem::path directory_;
    };
}