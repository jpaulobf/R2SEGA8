#include "Window/NtscShaderPipeline.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace R2SEGA8::Window;
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected preset path");
        NtscShaderPipeline shader;
        std::string error;
        if (!shader.initialize(argv[1], error))
            throw std::runtime_error(error);
        std::vector<uint32_t> pixels(32 * 24), output, repeated, resized;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 32; ++x)
                pixels[y * 32 + x] = (x > y && x < y + 8) ? 0xffff2000 : 0xff1020e0;
        if (!shader.render(pixels.data(), 32, {32, 24}, {64, 48}, output, error) ||
            !shader.render(pixels.data(), 32, {32, 24}, {64, 48}, repeated, error))
            throw std::runtime_error(error);
        if (output.size() != 64 * 48 || output != repeated)
            throw std::runtime_error("Wrong output size or unstable output");
        unsigned changed = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
                if ((output[y * 64 + x] & 0xffffff) != (pixels[(y / 2) * 32 + x / 2] & 0xffffff))
                    ++changed;
        if (changed < 20)
            throw std::runtime_error("Output does not smooth the diagonal image");
        if (std::filesystem::path(argv[1]).filename() == "scalefx-visual.cgp")
        {
            // ScaleFX selects original colors; sampling a metadata pass in
            // place of the original frame produces colors outside this palette.
            for (uint32_t pixel : output)
            {
                const uint32_t rgb = pixel & 0xffffff;
                if (rgb != 0xff2000 && rgb != 0x1020e0)
                    throw std::runtime_error("ScaleFX output contains a color absent from the source");
            }
        }
        if (!shader.render(pixels.data() + 32 + 1, 32, {30, 22}, {90, 66}, resized, error))
            throw std::runtime_error(error);
        if (resized.size() != 90 * 66)
            throw std::runtime_error("Crop or resize failed");
        std::fill(pixels.begin(), pixels.end(), 0xff8040c0);
        if (!shader.render(pixels.data(), 32, {32, 24}, {64, 48}, output, error))
            throw std::runtime_error(error);
        const uint32_t center = output[24 * 64 + 32];
        for (int shift : {0, 8, 16})
            if (std::abs(int((center >> shift) & 255) - int((0xff8040c0u >> shift) & 255)) > 2)
                throw std::runtime_error("Uniform color was not preserved");
        std::cout << "Shader GPU passes passed; changed " << changed << " pixels\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}