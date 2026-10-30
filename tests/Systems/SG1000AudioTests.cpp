#include "Systems/SG1000/SG1000.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    // Falha com a propriedade de integração que deixou de ser atendida.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        std::filesystem::path path;
        // Cria uma ROM própria que programa um tom, silencia os demais canais e entra em HALT.
        Fixture()
        {
            path = std::filesystem::temp_directory_path() /
                   ("r2sega8-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sg");
            const std::uint8_t code[] = {0x3e, 0x9f, 0xd3, 0x7f, 0x3e, 0xbf, 0xd3, 0x40, 0x3e, 0xdf, 0xd3, 0x55, 0x3e, 0xff, 0xd3,
                                         0x60, 0x3e, 0x8e, 0xd3, 0x7e, 0x3e, 0x0f, 0xd3, 0x7f, 0x3e, 0x90, 0xd3, 0x40, 0x76};
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(code), sizeof(code));
            file.close();
            check(static_cast<bool>(file), "Unable to write audio fixture");
        }
        // Remove somente a ROM criada para esta execução do teste.
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };
}

int main()
{
    try
    {
        using namespace R2SEGA8;
        Fixture fixture;
        Systems::SG1000 board{Core::Cartridge(fixture.path)};
        for (unsigned port = 0x40; port <= 0x7f; ++port)
        {
            board.output(static_cast<std::uint16_t>(0x1200 | port), static_cast<std::uint8_t>(0x80 | (port & 15)));
            check((board.psg().state().tone[0] & 15) == (port & 15), "PSG port aliases/high byte decoding");
        }
        const auto tone = board.psg().state().tone[0];
        board.output(0x00, 0x81);
        board.output(0xc0, 0x82);
        check(board.psg().state().tone[0] == tone && board.input(0x7f) == 0xff, "Unrelated ports must not reach PSG");

        // Uma escrita deixa READY baixo: a CPU para no próximo ciclo, mas os dois chips continuam avançando.
        const auto before = board.cpu().state().pc;
        const auto vdpBefore = board.vdp().frame().number;
        board.runCycles(20);
        check(board.cpu().state().pc == before && !board.psg().ready() && board.psg().state().clocks == 20,
              "PSG READY must hold CPU WAIT while the sound clock advances");
        board.runCycles(12);
        check(board.psg().ready() && !board.cpu().state().wait, "READY must release WAIT after 32 clocks");
        board.runCycles(1000);
        check(board.cpu().state().halted && board.psg().state().tone[0] == 254 && board.psg().state().attenuation[0] == 0 &&
                  board.psg().state().attenuation[1] == 15 && board.psg().state().attenuation[2] == 15 &&
                  board.psg().state().attenuation[3] == 15,
              "CPU OUT instructions must program the real PSG through all port mirrors");
        board.runCycles(357954);
        check(board.psg().state().clocks == board.cpu().state().cycles && board.videoFrame().number > vdpBefore,
              "CPU, VDP and PSG must advance together, including HALT and WAIT");
        std::vector<float> samples;
        board.takeAudio(samples);
        check(samples.size() == board.cpu().state().cycles * 48000 / 3579545 &&
                  std::any_of(samples.begin(), samples.end(), [](float v)
                              { return v > 0.01f || v < -0.01f; }),
              "Board must publish non-silent PCM with the correct clock/sample ratio");
        board.takeAudio(samples);
        check(samples.empty(), "Audio collection must consume samples once");
        const auto clocks = board.psg().state().clocks;
        const auto programmed = board.psg().state().tone;
        board.cpu().reset();
        check(board.psg().state().clocks == clocks && board.psg().state().tone == programmed,
              "CPU-only reset must not reset the sound chip");
        Systems::SG1000 fresh{Core::Cartridge(fixture.path)};
        check(fresh.psg().state().clocks == 0 && fresh.psg().state().tone[0] == 0, "ROM replacement must create a fresh PSG");
        std::cout << "SG-1000 audio integration tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}