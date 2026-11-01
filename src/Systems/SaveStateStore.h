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
        explicit SaveStateStore(std::filesystem::path directory);

        void save(const Core::Board &board, unsigned slot) const;
        std::vector<std::uint8_t> load(const Core::Board &board, unsigned slot) const;

    private:
        std::filesystem::path path(const Core::Board &board, unsigned slot) const;
        std::filesystem::path directory_;
    };
}