#pragma once

#include "Common/SystemType.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace R2SEGA8::Window
{
    // Dados de apresentação da seleção; não carrega nem mantém um cartucho emulado.
    struct RomPreview
    {
        std::filesystem::path path;
        std::size_t size = 0;
        Common::SystemType suggestedSystem = Common::SystemType::Unknown;
        bool hasHeader = false;
        bool conflictingHints = false;
    };

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
        using InspectRom = std::function<RomPreview(const std::filesystem::path &)>;
        using OpenRom = std::function<void(const std::filesystem::path &, Common::SystemType)>;

        // Solicita uma prévia; falhas são apresentadas no seletor de arquivos.
        InspectRom inspectRom;

        // Solicita o carregamento confirmado; falhas mantêm o diálogo aberto.
        OpenRom openRom;

        // Consulta os bytes para apresentação, antes de emitir ações que possam invalidá-los.
        std::function<MemoryView()> memory;

        // Consulta o quadro para copiar à textura antes de emitir ações que possam invalidar seus bytes.
        std::function<FrameView()> frame;

        // Notifica o pedido de encerramento pelo menu, pela janela ou pelo sistema operacional.
        std::function<void()> exit;
    };
}