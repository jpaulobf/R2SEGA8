#pragma once
#include "Core/Board.h"
#include "Core/Cartridge/SegaMapper.h"
#include "Core/IO/ControllerInput.h"
#include "Core/Audio/SN76489/SN76489.h"
#include "Core/VDP/SegaVDP/SegaVDP.h"

namespace R2SEGA8::Core
{
    // Implementação inicial de memória, vídeo, controles e áudio estéreo.
    class GameGear final : public Board
    {
    public:
        // Recebe o cartucho e aloca 8 KiB de RAM interna inicialmente zerada.
        explicit GameGear(Core::Cartridge cartridge);

        // Informa que a CPU já pode acessar o mapa de memória da placa.
        bool hasMemoryMap() const override { return true; }

        // O estado inclui memória, CPU, vídeo, áudio estéreo e controles.
        bool supportsSaveState() const override { return true; }
        std::vector<std::uint8_t> saveState() const override;
        void loadState(const std::vector<std::uint8_t> &data) override;

        // Retorna o clock da CPU no perfil NTSC.
        unsigned clockFrequency() const override { return 3579545; }

        // Retorna os T-states correspondentes a um quadro completo do VDP.
        unsigned cyclesPerFrame() const override { return SegaVDP::MasterTicksPerFrame / 3; }

        // Retorna a área visível do último quadro completo do Game Gear.
        VideoFrame videoFrame() const override { return vdp_.frame(); }

        // Informa que o áudio possui dois canais intercalados.
        unsigned audioChannels() const override { return 2; }

        // Transfere as amostras estéreo geradas desde a última coleta.
        void takeAudio(std::vector<float> &samples) override { psg_.takeSamples(samples); }

        // Aplica as preferências de volume e ativação ao PSG.
        void setAudioSettings(const Common::AudioSettings &settings) override { psg_.setAudioSettings(settings); }

        // Expõe o PSG para inspeção sem permitir sua alteração.
        const SN76489 &psg() const { return psg_; }
        
        // Controla a apresentação dos sprites e do fundo.
        void setRenderLayers(bool sprites, bool tiles) override
        {
            vdp_.setRenderSprites(sprites);
            vdp_.setRenderTiles(tiles);
        }
        
        // Configura o limite de sprites na apresentação do VDP.
        void setSpriteUnlimited(bool enabled) override { vdp_.setSpriteUnlimited(enabled); }
        
        // Atualiza os botões lógicos de um jogador, preservando o outro.
        void setButtons(unsigned player, Common::ButtonMask buttons) override;
        
        // Atualiza as intenções de entrada e a fase do turbo no quadro atual.
        void setInput(const Common::InputState &buttons) override;
        
        // Lê a ROM bancária ou a RAM pelo mapeador Sega.
        std::uint8_t readMemory(std::uint16_t address) const override;
        
        // Escreve na RAM ou nos registradores do mapeador Sega.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;
        
        // Lê Start, direcional, botões, contador vertical ou portas do VDP.
        std::uint8_t input(std::uint16_t port) override;
        
        // Encaminha escritas às portas do VDP, do PSG e do estéreo.
        void output(std::uint16_t port, std::uint8_t value) override;
        
        // Avança VDP, PSG e turbo conforme o clock dos periféricos.
        void clock(const Z80Pins &) override;
        
        // Expõe o VDP para inspeção sem permitir sua alteração.
        const SegaVDP &vdp() const { return vdp_; }

    private:
        SegaMapper mapper_;
        IO::ControllerInput controls_;
        SegaVDP vdp_{SegaVDP::Model::GameGear};
        SN76489 psg_{SN76489::DefaultClock, SN76489::DefaultSampleRate, SN76489::Variant::Sega, false, true};
    };
}