#include "Systems/System.h"
#include "Window/Window.h"
#include <exception>
#include <iostream>
#include <utility>

int main()
{
    try
    {
        std::unique_ptr<R2SEGA8::Systems::System> system;
        R2SEGA8::Window::Window window([&](R2SEGA8::Core::Cartridge cartridge, R2SEGA8::Common::SystemType type)
                                       {
            auto next = R2SEGA8::Systems::createSystem(type, std::move(cartridge));
            system = std::move(next); });
        window.open();
        while (window.processEvents())
            window.render(system.get());
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "R2SEGA8: " << error.what() << '\n';
        return 1;
    }
}