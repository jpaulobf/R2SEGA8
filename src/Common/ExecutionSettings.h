#pragma once

namespace R2SEGA8::Common
{
    // Preferências da sessão; não alteram clocks ou registradores do hardware emulado.
    struct ExecutionSettings
    {
        bool vsync = true;
        bool fastForwardEnabled = true;
    };
}