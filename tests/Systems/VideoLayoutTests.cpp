#include "Window/VideoLayout.h"
#include "Window/VideoEffects.h"
#include <cmath>
#include <stdexcept>

static void check(bool value)
{
    if (!value)
        throw std::runtime_error("Incorrect video layout");
}

int main()
{
    const auto aspect = R2Window::videoLayout(1920, 1080, 256, 192, false);
    check(aspect.x == 240 && aspect.y == 0 && aspect.width == 1440 && aspect.height == 1080);
    const auto stretched = R2Window::videoLayout(1920, 1080, 160, 144, true);
    check(stretched.x == 0 && stretched.y == 0 && stretched.width == 1920 && stretched.height == 1080);
    const auto tall = R2Window::videoLayout(900, 1200, 160, 144, false);
    check(tall.x == 0 && std::abs(tall.y - 195) < 0.01f && tall.width == 900 && tall.height == 810);
    const auto nes = R2Window::videoLayoutInteger(1920, 1080, 8.0 / 7.0, false);
    check(nes.x == 343 && nes.y == 0 && nes.width == 1234 && nes.height == 1080);
    const auto cropped = R2Window::videoLayoutInteger(1920, 1080, 60.0 / 49.0, false);
    check(cropped.x == 299 && cropped.y == 0 && cropped.width == 1322 && cropped.height == 1080);
    check(R2Window::videoLayout(0, 1080, 256, 192, false).width == 0);

    const std::uint8_t source[] = {
        1, 2, 3, 4, 100, 80, 60, 255,
        5, 6, 7, 8, 200, 160, 120, 254};
    std::vector<std::uint8_t> filtered;
    R2SEGA8::Window::applyScanlines(source, 8, 1, 2, 1, 25, filtered);
    check(filtered == std::vector<std::uint8_t>({100, 80, 60, 255, 150, 120, 90, 254}));
    check(source[12] == 200 && source[15] == 254);
}