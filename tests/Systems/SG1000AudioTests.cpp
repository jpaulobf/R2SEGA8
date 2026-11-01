#include "Core/Systems/SG1000/SG1000.h"
#include "Common/Hash.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
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
        Core::SG1000 board{Core::Cartridge(fixture.path)};
        Core::Board &boardInterface = board;
        check(board.vdp().state().spriteUnlimited, "Unlimited sprites must be enabled by default");
        boardInterface.setSpriteUnlimited(false);
        check(!board.vdp().state().spriteUnlimited, "Board must apply the sprite limit to the VDP");
        boardInterface.setSpriteUnlimited(true);
        check(board.vdp().state().spriteUnlimited, "Board must restore unlimited sprites");
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
        Core::SG1000 fresh{Core::Cartridge(fixture.path)};
        check(fresh.psg().state().clocks == 0 && fresh.psg().state().tone[0] == 0, "ROM replacement must create a fresh PSG");
        // Captura durante uma instrução e verifica a mesma continuação da máquina completa.
        auto original = std::make_unique<Core::SG1000>(Core::Cartridge(fixture.path));
        original->runCycles(101);
        original->writeMemory(0xc123, 0x5a);
        const auto saved = original->saveState();
        std::uint64_t baseline = 14695981039346656037ull;
        for (auto byte : saved)
            baseline = (baseline ^ byte) * 1099511628211ull;
        check(saved.size() == 416926 && baseline == 5792043530249614582ull,
              "Save state format must remain compatible with version 1");
        auto restored = std::make_unique<Core::SG1000>(Core::Cartridge(fixture.path));
        restored->loadState(saved);
        check(restored->saveState() == saved, "State must round-trip without losing CPU, VDP, PSG or RAM");
        original->runCycles(15000);
        restored->runCycles(15000);
        check(original->saveState() == restored->saveState(), "Restored machine must continue deterministically");
        auto bad = saved;
        bad.pop_back();
        bool rejected = false;
        try
        {
            restored->loadState(bad);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "Truncated state must be rejected");
        for (const std::size_t length : {40u, 10000u, 210000u, static_cast<unsigned>(saved.size() - 9)})
        {
            bad.assign(saved.begin(), saved.begin() + length);
            const auto checksum = Common::fnv1a64(bad);
            for (unsigned i = 0; i < 8; ++i)
                bad.push_back(static_cast<std::uint8_t>(checksum >> (i * 8)));
            rejected = false;
            try
            {
                restored->loadState(bad);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "A truncated payload with a valid checksum must be rejected");
        }
        bad = saved;
        bad[0] ^= 1;
        rejected = false;
        try
        {
            restored->loadState(bad);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "Wrong system magic must be rejected");
        std::cout << "SG-1000 audio integration tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}