#pragma once

#include "Common/SystemType.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace R2SEGA8::Core
{
    // Perfis físicos conhecidos; Automatic identifica imagens catalogadas pelo conteúdo.
    enum class CartridgeProfile
    {
        Automatic,
        PlainROM,
        CastleRAM
    };

    // Campos do cabeçalho Sega encontrados na imagem, sem certificação de validade.
    struct SegaHeader
    {
        std::size_t offset;
        std::uint8_t region;
        std::uint8_t sizeCode;
    };

    // Mantém a imagem original e as sugestões de identificação do cartucho.
    // As janelas de acesso da CPU e a seleção de bancos pertencem aos mapeadores.
    class Cartridge
    {
    public:
        // Lê a ROM, normaliza seu caminho absoluto e procura sugestões por extensão
        // e cabeçalho. Propaga as exceções de acesso ao arquivo e leitura.
        // O perfil explícito permite configurar cartuchos sem depender de seu nome de arquivo.
        explicit Cartridge(const std::filesystem::path &path, CartridgeProfile profile = CartridgeProfile::Automatic);

        // Retorna o perfil resolvido; Automatic nunca permanece após o carregamento.
        CartridgeProfile profile() const { return profile_; }

        // Expõe a RAM volátil do cartucho para inspeção, separada da RAM interna da placa.
        const std::vector<std::uint8_t> &ram() const { return ram_; }

        // Permite ao mapeador da placa atualizar a memória física do cartucho.
        std::vector<std::uint8_t> &ram() { return ram_; }

        // Retorna o caminho absoluto normalizado do arquivo de origem.
        const std::filesystem::path &path() const { return path_; }

        // Expõe os bytes originais da ROM para leitura, sem cópia ou recorte.
        const std::vector<std::uint8_t> &rom() const { return rom_; }

        // Retorna os campos do cabeçalho Sega, quando uma assinatura foi encontrada.
        const std::optional<SegaHeader> &header() const { return header_; }

        // Retorna a sugestão de sistema; um cabeçalho reconhecido prevalece sobre a extensão.
        Common::SystemType suggestedSystem() const { return suggested_; }

        // Informa se a extensão e o cabeçalho reconhecido sugerem sistemas diferentes.
        bool conflictingHints() const { return conflicting_; }

    private:
        std::filesystem::path path_;
        std::vector<std::uint8_t> rom_;
        std::vector<std::uint8_t> ram_;
        CartridgeProfile profile_ = CartridgeProfile::PlainROM;
        std::optional<SegaHeader> header_;
        Common::SystemType suggested_ = Common::SystemType::Unknown;
        bool conflicting_ = false;
    };
}