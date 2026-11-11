#include "Systems/PauseState.h"
#include <stdexcept>

int main()
{
    R2SEGA8::Systems::PauseState state;
    const auto check = [](bool value)
    { if (!value) throw std::runtime_error("pause causes must remain independent"); };
    check(!state.paused());
    state.toggleManual();
    state.setMenuPaused(true);
    state.setMinimized(true);
    state.setDebugPaused(true);
    state.setMenuPaused(false);
    state.setMinimized(false);
    state.setDebugPaused(false);
    check(state.paused());
    state.toggleManual();
    check(!state.paused());
    state.setDebugPaused(true);
    state.setMinimized(true);
    state.setDebugPaused(false);
    check(state.paused());
    state.setMinimized(false);
    check(!state.paused());
}