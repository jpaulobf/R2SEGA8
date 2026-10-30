#pragma once
#include "Core/Board.h"

namespace R2SEGA8::Core
{
    // Representa o SC-3000 com cartucho e RAM interna para inspeção.
    // O mapa de memória, os periféricos e as expansões ainda não são implementados.
    class SC3000 final : public Board
    {
    public:
        // Recebe o cartucho e aloca 1 KiB de RAM interna, sem RAM adicional de cartucho.
        explicit SC3000(Core::Cartridge cartridge);
    };
}