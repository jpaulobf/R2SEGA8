#pragma once
#include "../VideoFrame.h"
#include <array>
#include <cstdint>

namespace R2SEGA8::Core
{
    // Perfil TMS9918A NTSC: interface, raster, backgrounds e sprites; com sequenciamento dos acessos à VRAM.
    class TMS9918
    {
    public:
        static constexpr unsigned Width = 256, Height = 192;
        static constexpr unsigned DotsPerLine = 342, LinesPerFrame = 262;
        static constexpr unsigned MasterTicksPerLine = DotsPerLine * 2;
        static constexpr unsigned MasterTicksPerFrame = MasterTicksPerLine * LinesPerFrame;
        static constexpr unsigned FrameBytes = Width * Height * 4;
        // Identifica o dono do último slot físico, permitindo inspecionar a arbitragem sem callbacks.
        enum class Access : std::uint8_t
        {
            None,
            Refresh,
            CPURead,
            CPUWrite,
            Name,
            Color,
            Pattern,
            SpriteY,
            SpriteAttribute,
            SpritePattern
        };
        static constexpr unsigned CPUAccessDelay = 14; // Sete dots de preparação, em ciclos do cristal.
        struct Background
        {
            std::uint8_t name = 0, pattern = 0, colors = 0, column = 0, mode = 0;
            std::uint16_t line = 0;
            bool named = false, ready = false;
        };
        struct Sprite
        {
            std::uint8_t index = 31, y = 0, x = 0, name = 0, attributes = 0;
            std::uint8_t left = 0, right = 0, row = 0, size = 8, scale = 1;
            bool visible = false;
        };
        struct State
        {
            std::array<std::uint8_t, 0x4000> vram{};
            std::array<std::uint8_t, 8> registers{};
            std::array<Background, 2> background{};
            // A célula em apresentação mantém formato e bytes até o fim, mesmo após trocar R0/R1.
            std::uint8_t outputMode = 0, outputStart = 0, outputWidth = 0;
            std::uint16_t outputLine = 0;
            bool outputValid = false;
            bool cpuAccessPending = false, cpuAccessWrite = false;
            std::uint8_t cpuAccessData = 0, cpuAccessDelay = 0, refreshRow = 0;
            Access access = Access::None;
            std::uint16_t accessAddress = 0;
            std::uint8_t accessData = 0;
            std::array<std::array<std::uint8_t, FrameBytes>, 2> images{};
            // Bancos separados para seleção, busca e saída; nenhuma linha inteira é desenhada antecipadamente.
            std::array<Sprite, 32> evaluatedSprites{}, nextSprites{}, sprites{};
            std::uint8_t selectedSprites = 0, nextSpriteCount = 0, spriteCount = 0;
            std::uint8_t extraFetchIndex = 4, extraFetchPhase = 0;
            std::uint16_t spriteTargetLine = 0;
            bool spriteEvaluationStopped = true;
            // Preferência do emulador: os testes de hardware solicitam explicitamente o limite de quatro.
            bool spriteUnlimited = true;
            bool evaluationSpriteUnlimited = false;
            std::uint16_t address = 0, dot = 0, line = 0;
            std::uint8_t readBuffer = 0, controlLatch = 0, status = 0x1f;
            std::uint8_t masterPhase = 0, drawingImage = 0, pattern = 0, colors = 0;
            bool secondControlByte = false;
            std::uint64_t masterTicks = 0, completedFrames = 0;
        };

        // Inicializa a VRAM zerada por convenção e aplica o reset do controlador.
        TMS9918();

        // Reinicia registradores, fases e apresentação, preservando a VRAM e a preferência de sprites.
        void reset();

        // Avança um ciclo do cristal; dois ciclos formam um pixel do raster.
        void tick();

        // Avança uma quantidade exata de ciclos do cristal, inclusive durante apagamento.
        void runMasterTicks(std::uint64_t count);

        // Entrega o buffer atual e agenda a próxima pré-busca; o endereço avança no slot da VRAM.
        std::uint8_t readData();

        // Atualiza o buffer de dados e agenda a escrita; a VRAM e o endereço mudam somente no slot disponível.
        void writeData(std::uint8_t value);

        // Retorna o status e limpa VBlank, colisão, quinto sprite e a sequência parcial de controle.
        std::uint8_t readStatus();

        // Recebe os dois bytes de endereço/comando ou a escrita de um registrador.
        void writeControl(std::uint8_t value);

        // Expõe o nível lógico de INT: VBlank pendente e interrupção habilitada em R1.
        bool interrupt() const;

        // Consulta registradores, VRAM e fases sem alterar os latches das portas.
        const State &state() const { return state_; }

        // Restaura também o raster e os quadros intermediários; valida fases e endereços.
        void restore(const State &state);

        // Expõe somente o último quadro completo; seus bytes permanecem válidos até o próximo clock/reset.
        VideoFrame frame() const;

        // Configura a próxima seleção: false mantém quatro sprites por linha; true permite até 32.
        void setSpriteUnlimited(bool enabled) { state_.spriteUnlimited = enabled; }

    private:
        State state_;
        // Incrementa e aplica a volta de 3FFF para 0000 ao endereço de acesso da CPU.
        void incrementAddress();

        // Retém um único pedido da CPU; outro acesso substitui o pedido pendente sem criar uma fila.
        void requestCPUAccess(bool writing, std::uint8_t value);

        // Executa um slot de VRAM: refresh, background, sprites ou pedido da CPU, sem acessos simultâneos.
        void clockMemory();

        // Consulta os bits de modo no formato interno Graphics I=0, Text=1, Graphics II=2, Multicolor=4.
        unsigned mode() const;

        // Aplica a mesma condição de modo/apagamento à seleção, busca, composição e colisão dos sprites.
        bool spritesEnabled() const;

        // Lê um byte e registra o proprietário do slot físico atual.
        std::uint8_t readVRAM(unsigned address, Access owner);

        // Busca nome, cor ou padrão em fases separadas; retorna se o slot pertence ao background.
        bool fetchBackground(unsigned cycle, unsigned videoMode);

        // Decide se o ciclo admite a CPU conforme o modo e a atividade vertical do raster.
        bool cpuSlot(unsigned cycle, unsigned videoMode, bool active) const;

        // Avança seleção e buscas de sprites nos dots reservados do raster.
        void clockSprites();

        // Examina uma posição Y da SAT e retém os índices selecionados para a linha seguinte.
        void evaluateSprite(unsigned index);

        // Executa uma das seis buscas de atributos/padrões, preservando os latches entre dots.
        void fetchSprite(Sprite &sprite, unsigned phase, unsigned targetLine, bool physical = true);

        // Compõe a posição horizontal usando o banco indicado e atualiza a flag de colisão.
        std::uint8_t spriteColor(const std::array<Sprite, 32> &sprites, unsigned count, int x);

        // Avança a saída da célula usando bytes e formato latentes, inclusive durante apagamento.
        std::uint8_t backgroundColor();

        // Converte a cor lógica em RGBA e desenha um pixel visível no quadro em construção.
        void drawPixel();
    };
}
