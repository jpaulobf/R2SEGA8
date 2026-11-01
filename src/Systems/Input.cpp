#include "Input.h"
#include <iostream>
#include <stdexcept>

namespace R2SEGA8::Systems
{
    Input::Input()
    {
        using Common::Button;
        for (auto key : {SDLK_W, SDLK_UP})
            mapKey(key, Button::Up);
        for (auto key : {SDLK_S, SDLK_DOWN})
            mapKey(key, Button::Down);
        for (auto key : {SDLK_A, SDLK_LEFT})
            mapKey(key, Button::Left);
        for (auto key : {SDLK_D, SDLK_RIGHT})
            mapKey(key, Button::Right);
        mapKey(SDLK_K, Button::A);
        mapKey(SDLK_J, Button::B);
        mapKey(SDLK_I, Button::TurboA);
        mapKey(SDLK_U, Button::TurboB);
        mapKey(SDLK_BACKSPACE, Button::Select);
        mapKey(SDLK_RETURN, Button::Start);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_UP, Button::Up);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, Button::Down);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_LEFT, Button::Left);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Button::Right);
        mapGamepad(SDL_GAMEPAD_BUTTON_EAST, Button::A);
        mapGamepad(SDL_GAMEPAD_BUTTON_SOUTH, Button::B);
        mapGamepad(SDL_GAMEPAD_BUTTON_NORTH, Button::TurboA);
        mapGamepad(SDL_GAMEPAD_BUTTON_WEST, Button::TurboB);
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

    bool Input::isFastForwardPressed() const
    {
        if (fastForwardPressed_)
            return true;
        for (const auto &entry : gamepads_)
            if (SDL_GamepadConnected(entry.second.handle.get()) &&
                SDL_GetGamepadAxis(entry.second.handle.get(), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
                return true;
        return false;
    }

    bool Input::isRewindPressed() const
    {
        if (rewindPressed_)
            return true;
        for (const auto &entry : gamepads_)
            if (SDL_GamepadConnected(entry.second.handle.get()) &&
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
            gamepads_.erase(event.gdevice.which);
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
            result[binding.player] |= Common::mask(binding.button);
        }
        for (const auto &entry : gamepads_)
        {
            const auto &device = entry.second;
            if (!SDL_GamepadConnected(device.handle.get()))
                continue;
            auto &buttons = result[device.player];
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
                if (original & Common::mask(Button::TurboA))
                    buttons |= Common::mask(Button::TurboB);
                if (original & Common::mask(Button::TurboB))
                    buttons |= Common::mask(Button::B);
                if (original & Common::mask(Button::B))
                    buttons |= Common::mask(Button::A);
                if (original & Common::mask(Button::A))
                    buttons |= Common::mask(Button::TurboA);
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