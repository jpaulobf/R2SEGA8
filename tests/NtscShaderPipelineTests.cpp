#include "Window/NtscShaderPipeline.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace R2SEGA8::Window;
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected preset path");
        NtscShaderPipeline pipeline;
        std::string error;
        if (!pipeline.initialize(argv[1], error))
            throw std::runtime_error(error);
        const int width = std::string(argv[1]).find("256px") != std::string::npos ? 256 : 160;
        const int height = width == 256 ? 192 : 144;
        std::vector<uint32_t> input(width * height), first, second, resized;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                input[y * width + x] = ((x / 8 + y / 8) % 2) ? 0xffff4000 : 0xff0080ff;
        if (!pipeline.render(input.data(), width, {width, height}, {320, 288}, first, error))
            throw std::runtime_error(error);
        if (!pipeline.render(input.data(), width, {width, height}, {320, 288}, second, error))
            throw std::runtime_error(error);
        if (first.size() != 320 * 288 || first == second ||
            std::all_of(first.begin(), first.end(), [&](uint32_t p)
                        { return p == first.front(); }))
            throw std::runtime_error("Output is uniform, wrong size, or frame modulation is absent");
        if (!pipeline.render(input.data() + 8 * width + 8, width, {width - 16, height - 16}, {360, 336}, resized, error))
            throw std::runtime_error(error);
        if (resized.size() != 360 * 336)
            throw std::runtime_error("Resize failed");
        std::cout << "NTSC GPU passes passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}