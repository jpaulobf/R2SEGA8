#pragma once

namespace R2SEGA8::Common
{
    enum class RewindLevel
    {
        Light,
        Normal,
        Precise
    };

    // Preferências da sessão; não alteram clocks ou registradores do hardware emulado.
    struct ExecutionSettings
    {
        bool vsync = true;
        bool bilinearFilterEnabled = false;
        bool pixelArtEnabled = false;
        bool scanlinesEnabled = false;
        unsigned scanlineLevel = 5;
        bool fastForwardEnabled = true;
        bool rewindEnabled = false;
        RewindLevel rewindLevel = RewindLevel::Normal;
        bool unlimitedSpritesEnabled = true;
        bool invert12Enabled = false;
        bool invertT1112Enabled = false;
    };
}