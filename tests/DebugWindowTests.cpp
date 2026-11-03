#include "Window/Window.h"
#include "Window/DebugWindow.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <stdexcept>
int main() {
    using namespace R2SEGA8::Window;
    Callbacks callbacks;
    int closed=0;
    callbacks.debugClose=[&] { ++closed; };
    callbacks.debugView=[] { DebugView v; v.available=true; return v; };
    callbacks.debugRead=[](std::uint16_t) { return std::uint8_t(0); };
    Window mainWindow({}, {256,192}); mainWindow.open();
    auto *context=ImGui::GetCurrentContext();
    int parentCount=0; SDL_Window **parents=SDL_GetWindows(&parentCount);
    if(parentCount!=1) throw std::runtime_error("Main window missing");
    SDL_Window *parent=parents[0]; SDL_free(parents);
    DebugWindow debug(callbacks, parent); debug.open(); debug.render();
    if(!debug.isOpen() || ImGui::GetCurrentContext()!=context) throw std::runtime_error("Debug context was not restored");
    int count=0; SDL_Window **windows=SDL_GetWindows(&count); SDL_WindowID id=0;
    for(int i=0;i<count;++i) if(std::string(SDL_GetWindowTitle(windows[i]))=="R2SEGA8 - Disassembler") id=SDL_GetWindowID(windows[i]);
    SDL_free(windows);
    if(!id) throw std::runtime_error("Debugger window missing");
    SDL_Window *native=SDL_GetWindowFromID(id);
    int width=0, x=0, y=0, px=0, py=0, pw=0;
    SDL_GetWindowSize(native,&width,nullptr);
    if(width!=375) throw std::runtime_error("Unexpected debugger width");
    SDL_SetWindowPosition(parent,100,120); debug.snapToParent();
    SDL_GetWindowPosition(parent,&px,&py); SDL_GetWindowSize(parent,&pw,nullptr); SDL_GetWindowPosition(native,&x,&y);
    if(x!=px+pw+1 || y!=py) throw std::runtime_error("Debugger did not follow main window");
    SDL_Event event{}; event.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED; event.window.windowID=id;
    if(!debug.processEvent(event) || debug.isOpen() || closed!=1 || ImGui::GetCurrentContext()!=context) throw std::runtime_error("Debugger close failed");
    debug.open(); debug.render(); debug.close(); mainWindow.render();
    if(closed!=2 || ImGui::GetCurrentContext()!=context) throw std::runtime_error("Debugger reopen failed");
}
