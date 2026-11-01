#include "Core/Systems/SG1000/SG1000.h"
#include "Systems/Input.h"
#include "Systems/RewindBuffer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace R2SEGA8;
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void history(Common::RewindLevel level, unsigned interval)
    {
        Systems::RewindBuffer history;
        history.configure(60, 1, level);
        for (unsigned frame = 0; frame < 700; ++frame)
        {
            const bool due = history.captureDue();
            check(due == (frame % interval == 0), "Incorrect capture interval");
            if (due)
                history.push({static_cast<std::uint8_t>(frame / interval & 255),
                              static_cast<std::uint8_t>(frame / interval >> 8)});
        }
        history.beginRewind();
        const unsigned slots = 600 / interval + 1;
        for (unsigned i = 0; i < slots; ++i)
        {
            const auto *state = history.step();
            check(state && (static_cast<unsigned>((*state)[0]) | static_cast<unsigned>((*state)[1]) << 8) == 699 / interval - i,
                  "Ring must return the newest ten seconds in reverse order");
            for (unsigned pause = 1; pause < interval; ++pause)
                check(history.step() == nullptr, "Rewind must respect the selected playback interval");
        }
        check(history.step() == nullptr, "Rewind must stop at the oldest retained state");
        history.endRewind();
        check(history.captureDue(), "Resuming must begin a new timeline immediately");
        history.push({42});
        history.beginRewind();
        check(history.step()->at(0) == 42, "A new timeline must be recorded after rewinding");
        history.clear();
        check(!history.configured() && !history.step(), "Clearing must discard the history");
    }

    void boardRestore()
    {
        const auto path = std::filesystem::temp_directory_path() /
                          ("r2sega8-rewind-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sg");
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        {
            std::ofstream file(path, std::ios::binary);
            file.put('\x76');
            check(static_cast<bool>(file), "ROM fixture");
        }
        auto board = std::make_unique<Core::SG1000>(Core::Cartridge(path));
        Systems::RewindBuffer history;
        history.configure(board->clockFrequency(), board->cyclesPerFrame(), Common::RewindLevel::Precise);
        for (unsigned frame = 0; frame < 4; ++frame)
        {
            board->writeMemory(0xc000, static_cast<std::uint8_t>(frame));
            if (history.captureDue())
                history.push(board->saveState());
            board->runCycles(board->cyclesPerFrame());
        }
        history.beginRewind();
        for (int expected = 2; expected >= 0; expected -= 2)
        {
            const auto *state = history.step();
            check(state != nullptr, "Missing board snapshot");
            board->loadState(*state);
            check(board->readMemory(0xc000) == expected, "Board must restore earlier RAM");
            check(history.step() == nullptr, "Precise rewind must hold each state for two frames");
        }
        check(history.step() == nullptr, "Restoration must stop at history end");
    }

    void controls()
    {
        check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK), SDL_GetError());
        {
            Systems::Input input;
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_CAPSLOCK;
            input.handleEvent(event, true);
            check(input.isRewindPressed() && input.update() == Common::InputState{}, "Caps Lock must activate only rewind");
            event.type = SDL_EVENT_KEY_UP;
            input.handleEvent(event, false);
            check(!input.isRewindPressed(), "Key release must work while input is captured");
            event.type = SDL_EVENT_KEY_DOWN;
            input.handleEvent(event, true);
            event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            input.handleEvent(event, true);
            check(!input.isRewindPressed(), "Focus loss must release rewind");
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            const auto id = SDL_AttachVirtualJoystick(&desc);
            check(id != 0, SDL_GetError());
            auto *joystick = SDL_OpenJoystick(id);
            check(joystick != nullptr, SDL_GetError());
            check(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768), SDL_GetError());
            check(input.initialize(), SDL_GetError());
            SDL_UpdateJoysticks();
            check(!input.isRewindPressed(), "Released L2 must not rewind");
            check(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767), SDL_GetError());
            SDL_UpdateJoysticks();
            check(input.isRewindPressed(), "L2 must activate rewind");
            SDL_CloseJoystick(joystick);
            SDL_DetachVirtualJoystick(id);
            SDL_UpdateJoysticks();
            check(!input.isRewindPressed(), "Disconnecting the gamepad must stop rewind");
            input.shutdown();
        }
        SDL_Quit();
    }
}

int main()
{
    try
    {
        history(Common::RewindLevel::Light, 10);
        history(Common::RewindLevel::Normal, 5);
        history(Common::RewindLevel::Precise, 2);
        boardRestore();
        controls();
        std::cout << "Rewind history, board restoration and controls passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}