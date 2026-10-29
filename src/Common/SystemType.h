#pragma once

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