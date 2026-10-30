#pragma once
#include <SDL3/SDL.h>
#include <cstddef>

namespace R2SEGA8::Systems
{
    // Reproduz PCM mono float; não conhece CPU, PSG, placa ou janela.
    class Audio
    {
    public:
        // Cria uma sessão sem inicializar SDL ou abrir um dispositivo.
        Audio() = default;

        // Fecha o dispositivo antes de liberar a referência ao subsistema SDL de áudio.
        ~Audio();

        // Impede duplicar a posse exclusiva do stream e do dispositivo.
        Audio(const Audio &) = delete;

        // Impede compartilhar recursos nativos por atribuição.
        Audio &operator=(const Audio &) = delete;

        // Abre a saída padrão, inicialmente pausada; retorna false e preserva SDL_GetError em falhas.
        bool initialize(unsigned sampleRate);

        // Fecha a sessão; aceita chamadas repetidas e deve preceder SDL_Quit.
        void shutdown();

        // Enfileira PCM; sem dispositivo descarta os dados. Usa 40 ms de preparação e limite de 120 ms.
        // Recomeça a preparação somente após excesso de fila ou clear; fila vazia não pausa a saída.
        // Retorna false em falhas SDL.
        bool submit(const float *samples, std::size_t count);

        // Pausa e descarta áudio pendente ao trocar a ROM; não interfere no estado emulado.
        bool clear();

        // Consulta bytes de entrada ainda na fila SDL em unidades de amostras mono; retorna zero sem stream.
        std::size_t queuedSamples() const;

        // Informa se a sessão já liberou o dispositivo para reprodução.
        bool playing() const { return playing_; }

    private:
        SDL_AudioStream *stream_ = nullptr;
        unsigned sampleRate_ = 0;
        bool initialized_ = false, playing_ = false;
    };
}