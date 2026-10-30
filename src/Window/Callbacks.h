#pragma once

#include "Common/SystemType.h"
#include "Common/AudioSettings.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

union SDL_Event;

namespace R2SEGA8::Window
{
    // Consulta somente de leitura; o proprietário mantém os bytes válidos durante a apresentação.
    struct ByteView
    {
        const std::uint8_t *data = nullptr;
        std::size_t size = 0;
    };

    struct MemoryView
    {
        ByteView rom;
        ByteView ram;
    };

    // Quadro de apresentação com bytes R, G, B, A; não identifica o chip ou a máquina de origem.
    struct FrameView
    {
        const std::uint8_t *rgba = nullptr;
        unsigned width = 0, height = 0;
        std::size_t pitch = 0;
    };

    // Contrato da interface: ações e consultas são atendidas pelo chamador.
    struct Callbacks
    {
        using OpenRom = std::function<void(const std::filesystem::path &, Common::SystemType)>;

        // Solicita carregamento imediato com o sistema da extensão; falhas mantêm o diálogo aberto.
        OpenRom openRom;

        // Consulta a presença de uma ROM para habilitar as ações de execução.
        std::function<bool()> hasRom;

        // Solicita a reinicialização completa da máquina com a ROM atual.
        std::function<void()> reset;

        // Solicita a liberação da máquina ativa, mantendo a janela aberta.
        std::function<void()> unload;

        // Consulta os bytes para apresentação, antes de emitir ações que possam invalidá-los.
        std::function<MemoryView()> memory;

        // Consulta o quadro para copiar à textura antes de emitir ações que possam invalidar seus bytes.
        std::function<FrameView()> frame;

        // Consulta os ajustes de áudio mantidos pela aplicação, sem conhecer o mixer ou a placa.
        std::function<Common::AudioSettings()> audioSettings;

        // Publica ativação e volumes escolhidos no menu Audio.
        std::function<void(const Common::AudioSettings &)> setAudioSettings;

        // Encaminha eventos do host e informa se a interface permite entrada de jogo neste instante.
        std::function<void(const SDL_Event &, bool)> inputEvent;

        // Notifica foco/captura/diálogos, inclusive quando não há eventos ou um menu abre durante render.
        std::function<void(bool)> inputAvailability;

        // Notifica o pedido de encerramento pelo menu, pela janela ou pelo sistema operacional.
        std::function<void()> exit;
    };
}