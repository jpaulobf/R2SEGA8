#include "Window/KeyboardConfig.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
static void check(bool value)
{
    if (!value)
        throw std::runtime_error("keyboard config failed");
}
// Verifica padrões, persistência, valores inválidos e falhas de gravação.
int main()
{
    const auto dir = fs::temp_directory_path() / ("r2sega-keys-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    try
    {
        const auto path = dir / "keyboard.ini";
        R2SEGA8::Window::KeyboardConfig keys(path);
        auto mapping = keys.load();
        check(mapping == R2Keyboard::defaults());
        mapping[0][0] = SDLK_Q;
        mapping[1][4] = 0;
        check(keys.save(mapping));
        R2SEGA8::Window::KeyboardConfig reloaded(path);
        check(reloaded.load() == mapping);
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << "bad\n";
        }
        check(reloaded.load()[0][0] == SDLK_W);
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << std::string(5000, 'x');
        }
        check(reloaded.load() == R2Keyboard::defaults());
        check(!reloaded.save(mapping) && fs::file_size(path) == 5000);
        const auto blocked = dir / "blocked";
        {
            std::ofstream out(blocked);
            out << "x";
        }
        R2SEGA8::Window::KeyboardConfig unwritable(blocked / "keyboard.ini");
        unwritable.load();
        check(!unwritable.save(mapping));
        fs::remove_all(dir);
    }
    catch (...)
    {
        fs::remove_all(dir);
        throw;
    }
}