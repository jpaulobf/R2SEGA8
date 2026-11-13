#pragma once
#include "Systems/KeyboardMapping.h"
#include <filesystem>

namespace R2SEGA8::Window
{
    // Lê e persiste as teclas, mantendo o formato do arquivo fora do diálogo.
    class KeyboardConfig
    {
    public:
        // Usa o caminho informado ou, por padrão, resources/keyboard.ini.
        explicit KeyboardConfig(std::filesystem::path path = "resources/keyboard.ini") : path_(std::move(path)) {}
        // Usa os padrões nas posições ausentes ou com valores inválidos.
        R2Keyboard::Mapping load();
        // Retorna false se não for possível concluir a gravação do mapeamento.
        bool save(const R2Keyboard::Mapping &mapping) const;

    private:
        std::filesystem::path path_;
        // Evita sobrescrever um arquivo que não pôde ser lido com segurança.
        bool canSave_ = true;
    };
}