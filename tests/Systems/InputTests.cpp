#include "Systems/Input.h"
#include <iostream>
#include <stdexcept>

namespace
{
    using Input = R2SEGA8::Systems::Input;
    using R2SEGA8::Common::Button;
    using R2SEGA8::Common::mask;
    // Falha com o contexto da propriedade de entrada.
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Session
    {
        // Inicializa somente o host de teste; gamepads são gerenciados pela classe Input.
        Session()
        {
            // Isola a suíte dos controles físicos conectados no desktop do usuário.
            SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xcafe/0xbabe", SDL_HINT_OVERRIDE);
            check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK), SDL_GetError());
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        }
        // Executa depois de destruir Input e os controles virtuais.
        ~Session() { SDL_Quit(); }
    };
    struct Pad
    {
        SDL_JoystickID id = 0;
        SDL_Joystick *joystick = nullptr;
        // Usa dispositivos virtuais reais da SDL para testar enumeração, botões, eixos e remoção.
        Pad()
        {
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.vendor_id = 0xcafe;
            desc.product_id = 0xbabe;
            desc.name = "R2SEGA8 virtual gamepad";
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            id = SDL_AttachVirtualJoystick(&desc);
            check(id != 0, SDL_GetError());
            joystick = SDL_OpenJoystick(id);
            check(joystick != nullptr, SDL_GetError());
            check(SDL_IsGamepad(id), "Virtual pad must use the SDL gamepad mapping");
        }
        // Fecha a referência de teste e desconecta somente o dispositivo criado por ela.
        ~Pad() { detach(); }
        // Impede duplicar a posse de um dispositivo virtual.
        Pad(const Pad &) = delete;
        // Impede transferir a posse por atribuição implícita.
        Pad &operator=(const Pad &) = delete;
        // Desconecta explicitamente para exercitar hotplug; o destrutor aceita a segunda chamada.
        void detach()
        {
            if (joystick)
                SDL_CloseJoystick(joystick);
            joystick = nullptr;
            if (id)
                SDL_DetachVirtualJoystick(id);
            id = 0;
        }
        // Altera um botão físico pelo mecanismo de atualização da SDL.
        void button(SDL_GamepadButton physical, bool down)
        {
            check(SDL_SetJoystickVirtualButton(joystick, physical, down), SDL_GetError());
        }
        // Altera um eixo físico, incluindo os limites da zona morta.
        void axis(SDL_GamepadAxis physical, Sint16 value) { check(SDL_SetJoystickVirtualAxis(joystick, physical, value), SDL_GetError()); }
    };
    // Entrega eventos reais da SDL, inclusive os gerados pelos dispositivos virtuais.
    void pump(Input &input)
    {
        SDL_UpdateJoysticks();
        SDL_Event event;
        while (SDL_PollEvent(&event))
            input.handleEvent(event, true);
    }
    // Simula uma tecla, sem depender do layout instalado ou do foco do desktop.
    void key(Input &input, SDL_Keycode code, bool down, bool enabled = true, SDL_Keymod mods = 0, bool repeat = false)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.key = code;
        event.key.mod = mods;
        event.key.repeat = repeat;
        input.handleEvent(event, enabled);
    }
    // Confere múltiplas teclas da mesma função, remapeamento e captura pela interface.
    void keyboard()
    {
        Input input;
        key(input, SDLK_W, true);
        key(input, SDLK_UP, true);
        key(input, SDLK_W, false);
        check(input.update()[0] == mask(Button::Up), "Releasing one key must preserve another held source");
        key(input, SDLK_UP, false);
        key(input, SDLK_J, true);
        check(input.update()[0] == mask(Button::A), "Default keyboard A mapping");
        input.mapKey(SDLK_J, Button::B);
        check(input.update()[0] == 0, "Remapping must release the old held binding");
        key(input, SDLK_J, true);
        check(input.update()[0] == mask(Button::B), "Remapped keyboard binding");
        check(input.update(false) == R2SEGA8::Common::InputState{} && input.update()[0] == 0, "Capture must release and forget held keys");
        key(input, SDLK_J, true, true, SDL_KMOD_CTRL);
        key(input, SDLK_W, true, true, 0, true);
        check(input.update()[0] == 0, "Modified shortcuts and key repeats must not start gameplay presses");
        input.mapKey(SDLK_F, Button::A, 1);
        key(input, SDLK_F, true);
        check(input.update()[1] == mask(Button::A), "Keyboard mapping for player two");
        SDL_Event lost{};
        lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        input.handleEvent(lost, true);
        check(input.update() == R2SEGA8::Common::InputState{}, "Focus loss must clear keyboard input");
    }
    // TAB é momentâneo e independente dos botões; captura e perda de foco nunca deixam avanço rápido preso.
    void fastForward()
    {
        Input input;
        check(!input.isFastForwardPressed(), "Fast-forward must start released");
        key(input, SDLK_TAB, true);
        check(input.isFastForwardPressed() && input.update() == R2SEGA8::Common::InputState{},
              "TAB must activate fast-forward without pressing a console button");
        key(input, SDLK_J, true);
        key(input, SDLK_TAB, true, true, 0, true);
        check(input.isFastForwardPressed() && input.update()[0] == mask(Button::A),
              "Held TAB and key repeats must preserve gameplay input");
        key(input, SDLK_TAB, false, false);
        check(!input.isFastForwardPressed(), "TAB release must work even when gameplay input is disabled");
        for (auto mods : {SDL_KMOD_CTRL, SDL_KMOD_ALT, SDL_KMOD_GUI})
        {
            key(input, SDLK_TAB, true, true, mods);
            check(!input.isFastForwardPressed(), "Modified TAB shortcuts must not start fast-forward");
        }
        key(input, SDLK_TAB, true, true, 0, true);
        check(!input.isFastForwardPressed(), "A repeat after capture must not restart fast-forward");
        key(input, SDLK_TAB, true, false);
        check(!input.isFastForwardPressed(), "Captured TAB must be ignored");
        key(input, SDLK_TAB, true);
        input.update(false);
        check(!input.isFastForwardPressed(), "Input capture must forget a held TAB");
        for (auto type : {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MINIMIZED})
        {
            key(input, SDLK_TAB, true);
            SDL_Event event{};
            event.type = type;
            input.handleEvent(event, true);
            check(!input.isFastForwardPressed(), "Focus loss and minimization must stop fast-forward");
        }
        key(input, SDLK_TAB, true);
        input.clear();
        check(!input.isFastForwardPressed(), "ROM/reset input clearing must release TAB");
        key(input, SDLK_TAB, true);
        input.shutdown();
        check(!input.isFastForwardPressed(), "Input shutdown must release TAB");
    }

    // Verifica dispositivos anteriores à inicialização, hotplug, mapeamento, mistura de fontes e vagas estáveis.
    void gamepads()
    {
        Pad first;
        Input input;
        check(input.initialize(), SDL_GetError());
        check(input.initialize(), "Input initialization must be repeatable");
        pump(input);
        check(input.gamepadCount() == 1, "Startup discovery and duplicate added events");
        Pad second;
        pump(input);
        check(input.gamepadCount() == 2, "Hotplug must open the second player");
        first.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        second.button(SDL_GAMEPAD_BUTTON_EAST, true);
        pump(input);
        check(input.update()[0] == mask(Button::A) && input.update()[1] == mask(Button::B), "Gamepads must remain independent players");
        key(input, SDLK_W, true);
        check(input.update()[0] == (mask(Button::A) | mask(Button::Up)), "Keyboard and gamepad combine by OR");
        key(input, SDLK_W, false);
        first.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        first.button(SDL_GAMEPAD_BUTTON_WEST, true);
        key(input, SDLK_J, true);
        pump(input);
        const auto combined = input.update()[0];
        check((combined & mask(Button::A)) && (combined & mask(Button::TurboA)), "Normal and turbo sources must both be retained");
        check(R2SEGA8::Common::controllerButtons(combined, 0) & 0x10, "Normal press wins over inactive turbo");
        key(input, SDLK_J, false);
        first.button(SDL_GAMEPAD_BUTTON_WEST, false);
        pump(input);
        first.axis(SDL_GAMEPAD_AXIS_LEFTX, 16000);
        pump(input);
        check(input.update()[0] == 0, "Stick dead-zone boundary");
        first.axis(SDL_GAMEPAD_AXIS_LEFTX, 16001);
        first.axis(SDL_GAMEPAD_AXIS_LEFTY, -20000);
        pump(input);
        check(input.update()[0] == (mask(Button::Right) | mask(Button::Up)), "Left stick axes must map to logical directions");
        check(input.update(false) == R2SEGA8::Common::InputState{}, "Disabled input must also suppress held gamepads");
        first.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
        first.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
        input.mapGamepad(SDL_GAMEPAD_BUTTON_EAST, Button::A);
        pump(input);
        check(input.update()[1] == mask(Button::A), "Gamepad remapping applies to both controllers");
        Pad waiting;
        pump(input);
        check(input.gamepadCount() == 2, "Only two controllers are assigned at once");
        first.detach();
        pump(input);
        check(input.gamepadCount() == 2 && input.update()[0] == 0 && input.update()[1] == mask(Button::A),
              "Removing P1 must preserve P2 and assign the waiting controller to the free slot");
        waiting.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
        pump(input);
        check(input.update()[0] == mask(Button::Left), "Replacement controller must occupy the available player slot");
        waiting.detach();
        pump(input);
        check(input.gamepadCount() == 1 && input.update()[0] == 0, "Device removal releases its buttons");
        SDL_Event remap{};
        remap.type = SDL_EVENT_GAMEPAD_REMAPPED;
        remap.gdevice.which = second.id;
        input.handleEvent(remap, false);
        check(input.gamepadCount() == 1, "Remapped event must not duplicate an open device");
        input.shutdown();
        input.shutdown();
        check(input.gamepadCount() == 0 && input.update() == R2SEGA8::Common::InputState{},
              "Shutdown must release devices and pressed states");
        check(input.initialize(), "Input session can restart");
        pump(input);
        check(input.gamepadCount() == 1, "Restart discovers remaining devices");
        input.shutdown();
    }
} // namespace
int main()
{
    try
    {
        Session session;
        keyboard();
        fastForward();
        gamepads();
        std::cout << "Keyboard bindings, gamepad discovery/hotplug, players, axes, capture and shutdown passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
