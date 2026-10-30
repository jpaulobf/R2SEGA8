#pragma once

namespace R2SEGA8::Systems
{
    // Coordena a execução da aplicação e a integração entre a máquina e a interface.
    class Engine
    {
    public:
        // Inicializa a sessão e executa eventos, carregamento, emulação e apresentação.
        // Respeita a razão de clocks por quadro da placa, cedendo a CPU até o próximo prazo.
        // Retorna zero ao encerrar normalmente ou um após registrar uma falha no console.
        int run();
    };
}