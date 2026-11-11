#include "Window/NtscShaderPipeline.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace R2SEGA8::Window;
int main(int argc, char **argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("Expected preset, stock shader and test directory");
        std::string error;
        NtscShaderPipeline shader;
        if (!shader.initialize(argv[1], error))
            throw std::runtime_error(error);
        std::vector<uint32_t> pixels(32 * 24, 0xff808080), output, repeated;
        if (!shader.render(pixels.data(), 32, {32, 24}, {128, 96}, output, error) ||
            !shader.render(pixels.data(), 32, {32, 24}, {128, 96}, repeated, error))
            throw std::runtime_error(error);
        if (output.size() != 128 * 96 || output != repeated)
            throw std::runtime_error("Unexpected CRT output size or unstable output");
        unsigned minValue = 255, maxValue = 0;
        for (int y = 8; y < 88; ++y)
        {
            unsigned v = output[y * 128 + 64] & 255;
            minValue = std::min(minValue, v);
            maxValue = std::max(maxValue, v);
        }
        if (maxValue < 20 || maxValue <= minValue + 3)
            throw std::runtime_error("CRT scanlines missing");
        if (!shader.render(pixels.data(), 32, {32, 24}, {96, 72}, output, error) || output.size() != 96 * 72)
            throw std::runtime_error("CRT resize failed: " + error);

        // Verifica a conversão sRGB independentemente da matemática dos efeitos CRT.
        const auto dir = std::filesystem::path(argv[3]);
        std::filesystem::create_directories(dir);
        if (std::filesystem::path(argv[1]).filename() == "crt-lottes.cgp")
        {
            const auto custom = dir / "lottes-bloom.cgp";
            {
                std::ofstream file(custom);
                const auto cg = std::filesystem::path(argv[1]).parent_path() / "shaders/crt-lottes.cg";
                file << "shaders = 1\nshader0 = \"" << std::filesystem::absolute(cg).generic_string()
                     << "\"\nfilter_linear0 = false\nparameters = \"bloomAmount\"\nbloomAmount = 1.0\n";
            }
            NtscShaderPipeline bloom;
            if (!bloom.initialize(custom, error))
                throw std::runtime_error(error);
            pixels.assign(32 * 24, 0xff000000);
            pixels[12 * 32 + 16] = 0xffffffff;
            std::vector<uint32_t> withoutBloom, withBloom;
            if (!shader.render(pixels.data(), 32, {32, 24}, {128, 96}, withoutBloom, error) ||
                !bloom.render(pixels.data(), 32, {32, 24}, {128, 96}, withBloom, error))
                throw std::runtime_error(error);
            if (withoutBloom == withBloom)
                throw std::runtime_error("Lottes bloomAmount preset override had no effect");
        }
        const auto preset = dir / "srgb-check.cgp";
        {
            std::ofstream file(preset);
            file << "shaders = 1\nshader0 = \"" << std::filesystem::absolute(argv[2]).generic_string()
                 << "\"\nsrgb_framebuffer0 = true\nfilter_linear0 = false\n";
        }
        NtscShaderPipeline srgb;
        if (!srgb.initialize(preset, error))
            throw std::runtime_error(error);
        pixels.assign(32 * 24, 0xff404040);
        if (!srgb.render(pixels.data(), 32, {32, 24}, {64, 48}, output, error))
            throw std::runtime_error(error);
        const unsigned encoded = output[24 * 64 + 32] & 255;
        if (encoded < 135 || encoded > 139)
            throw std::runtime_error("sRGB target did not encode linear 0x40 correctly");
        std::cout << "CRT shader and sRGB target passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}