#pragma once
#include "Core/VideoFrame.h"
#include <array>
#include <cstdint>

namespace R2SEGA8::Core
{
    // Perfil TMS9918A NTSC: interface, temporização e os quatro modos documentados de background.
    // Sprites e arbitragem dos acessos físicos à VRAM serão implementados depois.
    class TMS9918
    {
      public:
        static constexpr unsigned Width = 256, Height = 192;
        static constexpr unsigned DotsPerLine = 342, LinesPerFrame = 262;
        static constexpr unsigned MasterTicksPerLine = DotsPerLine * 2;
        static constexpr unsigned MasterTicksPerFrame = MasterTicksPerLine * LinesPerFrame;
        static constexpr unsigned FrameBytes = Width * Height * 4;
        struct State
        {
            std::array<std::uint8_t, 0x4000> vram{};
            std::array<std::uint8_t, 8> registers{};
            std::array<std::array<std::uint8_t, FrameBytes>, 2> images{};
            std::uint16_t address = 0, dot = 0, line = 0;
            std::uint8_t readBuffer = 0, controlLatch = 0, status = 0x1f;
            std::uint8_t masterPhase = 0, drawingImage = 0, pattern = 0, colors = 0;
            bool secondControlByte = false;
            std::uint64_t masterTicks = 0, completedFrames = 0;
        };

        // Inicializa a VRAM zerada por convenção e aplica o reset do controlador.
        TMS9918();

        // Reinicia registradores, fases e apresentação, preservando os bytes da VRAM.
        void reset();

        // Avança um ciclo do cristal; dois ciclos formam um pixel do raster.
        void tick();

        // Avança uma quantidade exata de ciclos do cristal, inclusive durante apagamento.
        void runMasterTicks(std::uint64_t count);

        // Entrega o buffer de leitura e prepara o próximo byte, incrementando o endereço de 14 bits.
        std::uint8_t readData();

        // Escreve na VRAM e no buffer de dados, incrementando o endereço de 14 bits.
        void writeData(std::uint8_t value);

        // Retorna o status e limpa flags de interrupção e a sequência parcial de controle.
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

      private:
        State state_;
        // Incrementa e aplica a volta de 3FFF para 0000 ao endereço de acesso da CPU.
        void incrementAddress();
        // Obtém a cor lógica do background, buscando os dados no início de cada célula do modo atual.
        std::uint8_t backgroundColor();

        // Converte a cor lógica em RGBA e desenha um pixel visível no quadro em construção.
        void drawPixel();
    };
} // namespace R2SEGA8::Core
