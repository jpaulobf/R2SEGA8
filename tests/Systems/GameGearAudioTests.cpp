#include "../Support/CartridgeFixture.h"
#include "Core/Systems/GameGear/GameGear.h"
#include <algorithm>
#include <chrono>
#include <cmath>
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
    struct Fixture
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("r2sega8-gg-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gg");
        Fixture()
        {
            std::ofstream file(path, std::ios::binary);
            file.put('\x76');
            file.close();
            check(bool(file), "ROM fixture failed");
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };
    void routing(const std::filesystem::path &path, unsigned channel, unsigned side)
    {
        auto board = std::make_unique<Core::GameGear>(::R2SEGA8::Tests::loadCartridge(path));
        board->output(0x7f, static_cast<std::uint8_t>(0x90 | (channel << 5)));
        board->output(0x106, static_cast<std::uint8_t>((side == 0 ? 0x10 : 1) << channel));
        board->runCycles(100000);
        std::vector<float> samples;
        board->takeAudio(samples);
        check(samples.size() == (100000ull * 48000 / 3579545) * 2 && board->audioChannels() == 2,
              "Game Gear must produce interleaved stereo at the correct sample rate");
        bool heard = false;
        for (std::size_t i = 0; i < samples.size(); i += 2)
        {
            heard |= std::abs(samples[i + side]) > 0.0001f;
            check(samples[i + 1 - side] == 0, "Stereo routing leaked into the disabled side");
        }
        check(heard, "Enabled PSG channel must produce sound on its selected side");
        check(board->psg().state().clocks == 100000 && !board->cpu().state().wait,
              "Game Gear PSG must follow the CPU without imposing WAIT");
        board->takeAudio(samples);
        check(samples.empty(), "Audio collection must consume the queue");
    }
}

int main()
{
    try
    {
        Fixture fixture;
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned side = 0; side < 2; ++side)
                routing(fixture.path, channel, side);
        auto board = std::make_unique<Core::GameGear>(::R2SEGA8::Tests::loadCartridge(fixture.path));
        board->output(0x40, 0x90);
        board->runCycles(10000);
        std::vector<float> samples;
        board->takeAudio(samples);
        check(!samples.empty(), "Default routing must produce samples");
        for (std::size_t i = 0; i < samples.size(); i += 2)
            check(samples[i] == samples[i + 1], "Default routing must send every channel to both sides");
        Common::AudioSettings settings;
        settings.master.enabled = false;
        board->setAudioSettings(settings);
        board->runCycles(10000);
        board->takeAudio(samples);
        check(!samples.empty() && std::all_of(samples.begin(), samples.end(), [](float v)
                                              { return v == 0; }),
              "Master mute must silence both stereo channels");
        std::cout << "Game Gear PSG and stereo routing passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}