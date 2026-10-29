#pragma once
#include "Systems/System.h"

namespace R2SEGA8::Systems
{
    // Representa o Game Gear com cartucho e RAM interna para inspeção.
    // O mapa de memória e os dispositivos específicos ainda não são implementados.
    class GameGear final : public System
    {
    public:
        // Recebe o cartucho e aloca 8 KiB de RAM interna inicialmente zerada.
        explicit GameGear(Core::Cartridge cartridge);
    };
}