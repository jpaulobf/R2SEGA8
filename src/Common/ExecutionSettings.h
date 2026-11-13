#pragma once
#include "ConsoleProfile.h"

namespace R2SEGA8::Common
{
    enum class RewindLevel
    {
        Light,
        Normal,
        Precise
    };

    // Preferências da sessão.
    struct ExecutionSettings
    {
        ConsoleProfile consoleProfile = ConsoleProfile::Auto;
        bool vsync = true;
        bool bilinearFilterEnabled = false;
        bool pixelArtEnabled = false;
        bool scanlinesEnabled = false;
        bool cropLeftEnabled = false;
        unsigned scanlineLevel = 5;
        bool fastForwardEnabled = true;
        bool rewindEnabled = false;
        RewindLevel rewindLevel = RewindLevel::Normal;
        bool unlimitedSpritesEnabled = true;
        bool cpuOverclockEnabled = false;
        float cpuMultiplier = 1.0f;
        bool renderSprites = true;
        bool renderTiles = true;
        bool invert12Enabled = false;
        bool lightPhaserEnabled = false;
        bool invertT1112Enabled = false;
        bool invertT1T221Enabled = false;
    };
}