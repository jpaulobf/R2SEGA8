#include "Input.h"
#include <iostream>
#include <stdexcept>

namespace R2SEGA8::Systems
{
    Input::Input()
    {
        using Common::Button;
        setKeyboardMapping(R2Keyboard::defaults());
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_UP, Button::Up);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, Button::Down);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_LEFT, Button::Left);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Button::Right);
        mapGamepad(SDL_GAMEPAD_BUTTON_EAST, Button::B);
        mapGamepad(SDL_GAMEPAD_BUTTON_SOUTH, Button::A);
        mapGamepad(SDL_GAMEPAD_BUTTON_NORTH, Button::TurboB);
        mapGamepad(SDL_GAMEPAD_BUTTON_WEST, Button::TurboA);
        mapGamepad(SDL_GAMEPAD_BUTTON_BACK, Button::Select);
        mapGamepad(SDL_GAMEPAD_BUTTON_START, Button::Start);
    }

    Input::~Input()
    {
        shutdown();
    }

    bool Input::initialize()
    {
        if (initialized_)
            return discoverGamepads();
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            return false;
        initialized_ = true;
        return discoverGamepads();
    }

    bool Input::discoverGamepads()
    {
        int count = 0;
        auto *ids = SDL_GetGamepads(&count);
        if (!ids)
            return false;
        // Libera a enumeração mesmo se a alocação de uma entrada na map lançar uma exceção.
        const std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> devices(ids, SDL_free);
        for (int i = 0; i < count; ++i)
            openGamepad(ids[i]);
        return true;
    }

    void Input::shutdown()
    {
        gamepads_.clear();
        clear();
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        initialized_ = false;
    }

    void Input::clear()
    {
        pressedKeys_.clear();
        fastForwardPressed_ = false;
        rewindPressed_ = false;
    }

    void Input::setPlayerJoystick(unsigned player, bool enabled)
    {
        if (player >= 2)
            throw std::out_of_range("Invalid input player.");
        if (playerJoystick_[player] == enabled)
            return;
        playerJoystick_[player] = enabled;
        for (auto it = pressedKeys_.begin(); it != pressedKeys_.end();)
            if (keyMap_.at(*it).player == player)
                it = pressedKeys_.erase(it);
            else
                ++it;
    }

    bool Input::playerUsesJoystick(unsigned player) const
    {
        if (player >= 2)
            throw std::out_of_range("Invalid input player.");
        return playerJoystick_[player];
    }

    int Input::playerForSlot(unsigned slot) const
    {
        for (unsigned player = 0; player < 2; ++player)
            if (playerJoystick_[player] && slot-- == 0)
                return static_cast<int>(player);
        return -1;
    }

    bool Input::isFastForwardPressed() const
    {
        if (fastForwardPressed_)
            return true;
        for (const auto &entry : gamepads_)
            if (playerForSlot(entry.second.player) >= 0 && SDL_GamepadConnected(entry.second.handle.get()) &&
                SDL_GetGamepadAxis(entry.second.handle.get(), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
                return true;
        return false;
    }

    bool Input::isRewindPressed() const
    {
        if (rewindPressed_)
            return true;
        for (const auto &entry : gamepads_)
            if (playerForSlot(entry.second.player) >= 0 && SDL_GamepadConnected(entry.second.handle.get()) &&
                SDL_GetGamepadAxis(entry.second.handle.get(), SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
                return true;
        return false;
    }

    void Input::openGamepad(SDL_JoystickID id)
    {
        if (!initialized_ || gamepads_.count(id) || gamepads_.size() >= 2)
            return;
        bool used[2]{};
        for (const auto &entry : gamepads_)
            used[entry.second.player] = true;
        Gamepad pad(SDL_OpenGamepad(id), SDL_CloseGamepad);
        if (!pad)
        {
            std::cerr << "Input: Failed to open gamepad: " << SDL_GetError() << '\n';
            return;
        }
        const unsigned player = used[0] ? 1 : 0;
        SDL_SetGamepadPlayerIndex(pad.get(), static_cast<int>(player));
        gamepads_.emplace(id, Device{std::move(pad), player});
    }

    void Input::mapKey(SDL_Keycode key, Common::Button button, unsigned player)
    {
        if (player >= 2)
            throw std::out_of_range("Invalid input player.");
        pressedKeys_.erase(key);
        keyMap_.insert_or_assign(key, Binding{button, player});
    }

    void Input::setKeyboardMapping(const R2Keyboard::Mapping &mapping)
    {
        clear();
        keyMap_.clear();
        using Common::Button;
        // A ordem acompanha as funções persistidas e apresentadas no editor.
        constexpr Button buttons[] = {Button::Up, Button::Down, Button::Left, Button::Right,
                                      Button::A, Button::B, Button::TurboA, Button::TurboB, Button::Start, Button::Select};
        for (unsigned p = 0; p < 2; ++p)
            for (int b = 0; b < 10; ++b)
                if (mapping[p][b])
                    mapKey(mapping[p][b], buttons[b], p);
    }

    void Input::mapGamepad(SDL_GamepadButton physical, Common::Button button)
    {
        if (physical < 0 || physical >= SDL_GAMEPAD_BUTTON_COUNT)
            throw std::out_of_range("Invalid gamepad button.");
        gamepadMap_.insert_or_assign(physical, button);
    }

    void Input::handleEvent(const SDL_Event &event, bool enabled)
    {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMAPPED)
            openGamepad(event.gdevice.which);
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            const auto removed = gamepads_.find(event.gdevice.which);
            if (removed != gamepads_.end())
            {
                const unsigned slot = removed->second.player;
                gamepads_.erase(removed);
                for (auto &entry : gamepads_)
                    if (entry.second.player > slot)
                    {
                        --entry.second.player;
                        SDL_SetGamepadPlayerIndex(entry.second.handle.get(), static_cast<int>(entry.second.player));
                    }
            }
            if (initialized_)
                discoverGamepads();
        }
        else if (event.type == SDL_EVENT_KEY_UP)
        {
            pressedKeys_.erase(event.key.key);
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = false;
            if (event.key.key == SDLK_CAPSLOCK)
                rewindPressed_ = false;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN && enabled && !event.key.repeat &&
                 !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        {
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = true;
            if (event.key.key == SDLK_CAPSLOCK)
                rewindPressed_ = true;
            if (keyMap_.count(event.key.key))
                pressedKeys_.insert(event.key.key);
        }
        if (!enabled || event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED)
            clear();
    }

    Common::InputState Input::update(bool enabled, const Common::ExecutionSettings &settings)
    {
        Common::InputState result{};
        if (!enabled)
        {
            clear();
            return result;
        }
        for (auto key : pressedKeys_)
        {
            const auto &binding = keyMap_.at(key);
            if (!playerJoystick_[binding.player])
                result[binding.player] |= Common::mask(binding.button);
        }
        for (const auto &entry : gamepads_)
        {
            const auto &device = entry.second;
            const int player = playerForSlot(device.player);
            if (player < 0 || !SDL_GamepadConnected(device.handle.get()))
                continue;
            auto &buttons = result[static_cast<unsigned>(player)];
            for (const auto &binding : gamepadMap_)
                if (SDL_GetGamepadButton(device.handle.get(), binding.first))
                    buttons |= Common::mask(binding.second);
            constexpr int deadZone = 16000;
            const auto x = SDL_GetGamepadAxis(device.handle.get(), SDL_GAMEPAD_AXIS_LEFTX);
            const auto y = SDL_GetGamepadAxis(device.handle.get(), SDL_GAMEPAD_AXIS_LEFTY);
            if (x < -deadZone)
                buttons |= Common::mask(Common::Button::Left);
            if (x > deadZone)
                buttons |= Common::mask(Common::Button::Right);
            if (y < -deadZone)
                buttons |= Common::mask(Common::Button::Up);
            if (y > deadZone)
                buttons |= Common::mask(Common::Button::Down);
        }
        using Common::Button;
        const auto actionMask = Common::mask(Button::A) | Common::mask(Button::B) |
                                Common::mask(Button::TurboA) | Common::mask(Button::TurboB);
        for (auto &buttons : result)
        {
            const auto original = buttons;
            if (settings.invertT1112Enabled)
            {
                buttons &= ~actionMask;
                if (original & Common::mask(Button::TurboA)) // u
                    buttons |= Common::mask(Button::A);
                if (original & Common::mask(Button::TurboB)) // i
                    buttons |= Common::mask(Button::TurboA);
                if (original & Common::mask(Button::B)) // k
                    buttons |= Common::mask(Button::TurboB);
                if (original & Common::mask(Button::A)) // j
                    buttons |= Common::mask(Button::B);
            }
            else if (settings.invertT1T221Enabled)
            {
                buttons &= ~actionMask;
                if (original & Common::mask(Button::TurboA)) // u
                    buttons |= Common::mask(Button::B);
                if (original & Common::mask(Button::TurboB)) // i
                    buttons |= Common::mask(Button::TurboB);
                if (original & Common::mask(Button::B)) // k
                    buttons |= Common::mask(Button::TurboA);
                if (original & Common::mask(Button::A)) // j
                    buttons |= Common::mask(Button::A);
            }
            else if (settings.invert12Enabled)
            {
                buttons &= ~(Common::mask(Button::A) | Common::mask(Button::B));
                if (original & Common::mask(Button::A))
                    buttons |= Common::mask(Button::B);
                if (original & Common::mask(Button::B))
                    buttons |= Common::mask(Button::A);
            }
        }
        return result;
    }
}