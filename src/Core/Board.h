#pragma once

#include "Common/Buttons.h"
#include "Common/AudioSettings.h"
#include "Core/CPU/Z80/Z80.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/VideoFrame.h"
#include <vector>

namespace R2SEGA8::Core
{
    // Representa a placa da máquina, reunindo CPU, cartucho, RAM e o contrato do barramento.
    // Cada sistema concreto determina seu mapeamento e suas particularidades.
    class Board : public Z80Bus
    {
    public:
        // Permite destruir a máquina concreta por um ponteiro para a classe base.
        virtual ~Board() = default;

        // Impede copiar a CPU e sua referência ao barramento desta máquina.
        Board(const Board &) = delete;

        // Impede substituir uma máquina com uma CPU conectada a outro barramento.
        Board &operator=(const Board &) = delete;

        // Expõe a CPU para execução controlada, interrupções e depuração.
        Z80 &cpu() { return cpu_; }

        // Expõe o estado da CPU sem permitir modificações.
        const Z80 &cpu() const { return cpu_; }

        // Executa exatamente o orçamento de T-states; exige mapa de memória implementado.
        std::uint64_t runCycles(std::uint64_t budget);

        // Retorna zero enquanto a máquina não oferecer um perfil de execução.
        virtual unsigned clockFrequency() const { return 0; }

        // Retorna a quantidade de T-states por quadro completo; zero indica ausência de temporização de vídeo.
        virtual unsigned cyclesPerFrame() const { return 0; }

        // Consulta o último quadro completo; placas sem vídeo implementado retornam uma consulta vazia.
        virtual VideoFrame videoFrame() const { return {}; }

        // Transfere PCM mono float a 48 kHz; placas sem áudio limpam o destino.
        virtual void takeAudio(std::vector<float> &samples) { samples.clear(); }

        // Recebe ajustes de apresentação; placas sem mixer implementado ignoram a atualização.
        virtual void setAudioSettings(const Common::AudioSettings &) {}

        // Recebe intenções lógicas do jogador; placas sem controles implementados ignoram a atualização.
        virtual void setButtons(unsigned, Common::ButtonMask) {}

        // Publica o estado dos dois jogadores na mesma atualização de entrada.
        virtual void setInput(const Common::InputState &buttons)
        {
            for (unsigned player = 0; player < buttons.size(); ++player)
                setButtons(player, buttons[player]);
        }

        // Retorna FF provisoriamente enquanto os periféricos não estiverem conectados.
        std::uint8_t input(std::uint16_t port) override;

        // Ignora provisoriamente saídas enquanto os periféricos não estiverem conectados.
        void output(std::uint16_t port, std::uint8_t value) override;

        // Retorna o tipo da máquina instanciada.
        Common::SystemType type() const { return type_; }

        // Expõe o cartucho da máquina para consulta, sem transferir sua posse.
        const Cartridge &cartridge() const { return cartridge_; }

        // Expõe os bytes físicos da RAM interna, sem aplicar espelhamento de endereços.
        const std::vector<std::uint8_t> &ram() const { return ram_; }

        // Indica se o perfil atual implementa acessos ao espaço de memória da CPU.
        virtual bool hasMemoryMap() const { return false; }

        // Lê um byte pelo endereço da CPU. A implementação base lança uma exceção
        // porque ainda não possui um mapa de memória.
        std::uint8_t readMemory(std::uint16_t address) const override;

        // Escreve um byte pelo endereço da CPU. A implementação base lança uma
        // exceção porque ainda não possui um mapa de memória.
        void writeMemory(std::uint16_t address, std::uint8_t value) override;

    protected:
        // Recebe o cartucho e aloca ramSize bytes de RAM interna, inicialmente zerados.
        Board(Common::SystemType type, Cartridge cartridge, std::size_t ramSize);
        Cartridge cartridge_;
        std::vector<std::uint8_t> ram_;

    private:
        Common::SystemType type_;
        Z80 cpu_;
    };
}