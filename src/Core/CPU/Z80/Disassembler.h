#pragma once
#include "Core/CPU/Z80/Z80.h"
#include <functional>
#include <string>

namespace R2SEGA8::Core
{
    struct Disassembled
    {
        std::uint16_t address, next;
        std::string bytes, text;
    };
    // Decodifica uma instrução usando leituras de memória fornecidas pelo chamador.
    Disassembled disassemble(std::uint16_t address, const std::function<std::uint8_t(std::uint16_t)> &read);
}