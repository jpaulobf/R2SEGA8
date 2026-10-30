#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace R2SEGA8::Common
{
    // Identifica as máquinas disponíveis; Unknown indica ausência de identificação.
    enum class SystemType
    {
        Unknown,
        SG1000,
        SC3000,
        MasterSystem,
        GameGear
    };

    // Identifica extensões de ROM conhecidas, sem ler arquivos ou inferir cabeçalhos de BIN/ZIP.
    inline SystemType systemFromExtension(std::string extension)
    {
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (extension == ".sg")
            return SystemType::SG1000;
        if (extension == ".sc")
            return SystemType::SC3000;
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
        case SystemType::SC3000:
            return "SC-3000";
        case SystemType::MasterSystem:
            return "Master System";
        case SystemType::GameGear:
            return "Game Gear";
        default:
            return "Unknown";
        }
    }
}