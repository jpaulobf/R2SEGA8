#pragma once
#include <algorithm>
#include <array>

namespace R2SEGA8::Common
{
    // Ajuste de apresentação: não representa um registrador do chip emulado.
    struct AudioControl
    {
        bool enabled = true;
        int volume = 100;

        // Converte a porcentagem em ganho linear e limita valores externos à faixa de 0 a 100.
        float gain() const { return enabled ? std::clamp(volume, 0, 100) / 100.0f : 0.0f; }
    };

    // Preferências da sessão; canais em ordem: Tone 1, Tone 2, Tone 3 e Noise.
    struct AudioSettings
    {
        AudioControl master;
        std::array<AudioControl, 4> channels{};
    };
}