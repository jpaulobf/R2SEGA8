#pragma once
#include "Systems/System.h"

namespace R2SEGA8::Systems
{
    // Representa o Master System com cartucho e RAM interna para inspeção.
    // O mapa de memória e os mapeadores ainda não são implementados.
    class MasterSystem final : public System
    {
    public:
        // Recebe o cartucho e aloca 8 KiB de RAM interna inicialmente zerada.
        explicit MasterSystem(Core::Cartridge cartridge);
    };
}