#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace R2SEGA8::Common
{
    // Os valores dos consoles fazem parte dos nomes dos arquivos de save state.
    enum class SystemType
    {
        Unknown = 0,
        SG1000 = 1,
        MasterSystem = 3,
        GameGear = 4
    };

    // Identifica extensões de ROM conhecidas, sem ler arquivos ou inferir cabeçalhos de BIN/ZIP.
    inline SystemType systemFromExtension(std::string extension)
    {
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (extension == ".sg")
            return SystemType::SG1000;
        if (extension == ".sms")
            return SystemType::MasterSystem;
        if (extension == ".gg")
            return SystemType::GameGear;
        return SystemType::Unknown;
    }

    // Retorna o nome de exibição do sistema, ou "Unknown" para valores desconhecidos.
    inline const char *systemName(SystemType type)
    {
        switch (type)
        {
        case SystemType::SG1000:
            return "SG-1000";
        case SystemType::MasterSystem:
            return "Master System";
        case SystemType::GameGear:
            return "Game Gear";
        default:
            return "Unknown";
        }
    }
}