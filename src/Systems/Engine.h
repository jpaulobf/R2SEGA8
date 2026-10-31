#pragma once

namespace R2SEGA8::Systems
{
    // Coordena a execução da aplicação e a integração entre a máquina e a interface.
    class Engine
    {
    public:
        // Inicializa a sessão e executa eventos, carregamento, emulação e apresentação.
        // Usa o clock nativo, a espera vertical ou TAB sem limite; o avanço rápido silencia a saída de áudio.
        // Retorna zero ao encerrar normalmente ou um após registrar uma falha no console.
        int run();
    };
}