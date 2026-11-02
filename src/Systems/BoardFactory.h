#pragma once
#include "Common/ConsoleProfile.h"
#include "Core/Board.h"
#include <memory>

namespace R2SEGA8::Systems
{
    // Constrói a máquina do tipo solicitado e transfere o cartucho para ela.
    // Lança uma exceção para tipos desconhecidos ou perfis não suportados.
    // O chamador só deve substituir a máquina ativa após a construção terminar.
    std::unique_ptr<Core::Board> createBoard(Common::SystemType type, Core::Cartridge cartridge, Common::ConsoleProfile profile = Common::ConsoleProfile::Auto);
}