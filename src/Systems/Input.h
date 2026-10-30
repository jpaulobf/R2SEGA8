#pragma once
#include "Common/Buttons.h"
#include <SDL3/SDL.h>
#include <map>
#include <memory>
#include <set>

namespace R2SEGA8::Systems
{
    // Adaptador do host: combina teclado e gamepads sem conhecer o hardware dos controles Sega.
    class Input
    {
    public:
        // Define WASD/setas, J/K/I/U, Select/Start e as posições padrão dos botões do gamepad.
        Input();

        // Libera gamepads e a referência ao subsistema SDL; shutdown deve preceder SDL_Quit da janela.
        ~Input();

        // Gamepads possuem dono exclusivo e não podem ser copiados.
        Input(const Input &) = delete;

        // Impede compartilhar a posse de dispositivos por atribuição.
        Input &operator=(const Input &) = delete;

        // Inicializa os gamepads e descobre os já conectados; o teclado funciona mesmo se falhar.
        bool initialize();

        // Fecha os dispositivos e o subsistema; aceita chamadas repetidas.
        void shutdown();

        // Processa hotplug e teclado; solturas e perda de foco liberam estados mesmo com captura da interface.
        void handleEvent(const SDL_Event &event, bool enabled);

        // Retorna intenções dos dois jogadores; turbo permanece lógico para seguir o clock emulado na placa.
        Common::InputState update(bool enabled = true);

        // Esquece teclas memorizadas; os gamepads são amostrados novamente em update.
        void clear();

        // Associa uma tecla a um botão/jogador e descarta sua pressão anterior.
        void mapKey(SDL_Keycode key, Common::Button button, unsigned player = 0);

        // Associa uma posição de botão físico, usando a mesma configuração nos dois gamepads.
        void mapGamepad(SDL_GamepadButton physical, Common::Button button);

        // Informa os dispositivos abertos para diagnóstico e testes de conexão/remoção.
        std::size_t gamepadCount() const { return gamepads_.size(); }

    private:
        struct Binding
        {
            Common::Button button;
            unsigned player;
        };
        using Gamepad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
        struct Device
        {
            Gamepad handle;
            unsigned player;
        };

        // Descobre dispositivos conectados sem deslocar a associação de jogadores já existentes.
        bool discoverGamepads();

        // Ocupa a primeira vaga livre entre os dois jogadores; ignora duplicações e controles excedentes.
        void openGamepad(SDL_JoystickID id);
        std::map<SDL_Keycode, Binding> keyMap_;
        std::map<SDL_GamepadButton, Common::Button> gamepadMap_;
        std::set<SDL_Keycode> pressedKeys_;
        std::map<SDL_JoystickID, Device> gamepads_;
        bool initialized_ = false;
    };
}