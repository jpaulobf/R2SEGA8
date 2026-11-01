#pragma once
#include "Common/AudioSettings.h"
#include <array>
#include <cstdint>
#include <vector>

namespace R2SEGA8::Core
{
    // SN76489AN discreto do SG-1000: três tons, ruído e atenuação de 2 dB por passo.
    // Não representa o PSG integrado dos consoles Sega posteriores.
    class SN76489
    {
    public:
        static constexpr unsigned DefaultClock = 3579545;
        static constexpr unsigned DefaultSampleRate = 48000;

        struct State
        {
            std::array<std::uint16_t, 3> tone{};
            std::array<std::uint8_t, 4> attenuation{};
            std::array<unsigned, 4> counter{};
            std::array<bool, 4> output{};
            // Os dois bits inferiores modelam o atraso da saída do LFSR de 15 bits do modelo AN.
            std::uint32_t noiseShift = 0x10000;
            std::uint8_t noise = 0, latch = 0;
            unsigned divider = 0, busyClocks = 0;
            std::uint64_t clocks = 0;
        };

        // Estado do chip e da integração PCM; a fila de saída e as preferências do usuário não pertencem à máquina.
        struct Snapshot
        {
            // A ordem integra o formato de save state da placa e deve permanecer estável.
            template <class Archive>
            void stateArchive(Archive &ar)
            {
                ar(state.tone, state.attenuation, state.counter, state.output, state.noiseShift, state.noise, state.latch,
                   state.divider, state.busyClocks, state.clocks, clockFrequency, sampleRate, samplePhase,
                   sampleArea, previousInput, previousOutput);
            }

            State state;
            unsigned clockFrequency = 0, sampleRate = 0, samplePhase = 0;
            double sampleArea = 0, previousInput = 0, previousOutput = 0;
        };

        // Configura os clocks de entrada e a taxa PCM; exige 0 < sampleRate <= clockFrequency.
        explicit SN76489(unsigned clockFrequency = DefaultClock, unsigned sampleRate = DefaultSampleRate);

        // Reinicia chip e amostrador; volumes máximos seguem o perfil AN, sem garantir estado elétrico de partida.
        void reset();

        // Aplica latch/data imediatamente e baixa READY por 32 clocks; o chamador respeita READY/WAIT.
        void write(std::uint8_t value);

        // Avança um clock de entrada, incluindo divisor interno, READY e integração da saída PCM.
        void tick();

        // Avança uma quantidade exata de clocks, sem depender de quadros de vídeo.
        void runClocks(std::uint64_t clocks);

        // Consulta registradores e contadores sem alterar o chip; não é um formato de save state.
        const State &state() const { return state_; }

        Snapshot snapshot() const;
        // Valida antes de alterar o chip; descarta PCM já gerado e mantém as preferências atuais.
        void restore(const Snapshot &snapshot);

        // Retorna o nível lógico de READY para ligação ao barramento da máquina.
        bool ready() const { return state_.busyClocks == 0; }

        // Consulta a mistura unipolar com ganhos por canal, antes da integração, remoção de DC e Master.
        float level() const { return level_; }

        // Transfere PCM mono float e reutiliza a alocação anterior do consumidor; mantém a fase do amostrador.
        void takeSamples(std::vector<float> &destination);

        // Ajusta somente a mistura de apresentação; preserva registradores, contadores e LFSR.
        void setAudioSettings(const Common::AudioSettings &settings);

    private:
        // Retorna o período de meia onda; zero equivale a 1024 no chip Texas Instruments.
        unsigned tonePeriod(unsigned channel) const;

        // Retorna o intervalo entre deslocamentos do ruído no domínio interno dividido por 16.
        unsigned noisePeriod() const;

        // Avança os contadores de tom e o LFSR no clock interno.
        void clockDivided();

        // Recalcula a mistura dos quatro canais sem modificar contadores ou fases.
        void updateLevel();

        // Integra um clock em janelas racionais, filtra DC e limita o armazenamento a 250 ms.
        void sampleClock();

        State state_;
        Common::AudioSettings audioSettings_;
        unsigned clockFrequency_, sampleRate_, samplePhase_ = 0;
        float level_ = 0, masterGain_ = 1;
        double levelPerClock_ = 0;
        std::array<float, 4> channelGains_{1, 1, 1, 1};
        double sampleArea_ = 0, previousInput_ = 0, previousOutput_ = 0, highPass_ = 0;
        std::array<float, 16> volume_{};
        std::vector<float> samples_;
    };
}