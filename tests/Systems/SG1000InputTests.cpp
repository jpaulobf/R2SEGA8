#include "Core/IO/Joypad.h"
#include "Core/Systems/SG1000/SG1000.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using Board = R2SEGA8::Core::SG1000;
    using VDP = R2SEGA8::Core::TMS9918;
    using R2SEGA8::Common::Button;
    using R2SEGA8::Common::mask;
    // Interrompe o teste com a propriedade esperada.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        std::filesystem::path path;
        // Cria ROM própria que lê DC/DD, sinaliza conclusão e conta NMIs em C010.
        Fixture()
        {
            path = std::filesystem::temp_directory_path() /
                   ("r2sega8-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sg");
            std::vector<std::uint8_t> rom(0x100, 0);
            const std::uint8_t code[] = {0xf3, 0x31, 0, 0xc4, 0xdb, 0xdc, 0x32, 0, 0xc0, 0xdb, 0xdd,
                                         0x32, 1, 0xc0, 0x3e, 1, 0x32, 2, 0xc0, 0x76, 0x18, 0xfd};
            const std::uint8_t nmi[] = {0xf5, 0x3a, 0x10, 0xc0, 0x3c, 0x32, 0x10, 0xc0, 0xf1, 0xed, 0x45};
            std::copy(std::begin(code), std::end(code), rom.begin());
            std::copy(std::begin(nmi), std::end(nmi), rom.begin() + 0x66);
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()), rom.size());
            file.close();
            check(bool(file), "Cannot write controller test ROM");
        }
        // Remove apenas o arquivo criado pelo próprio teste.
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

    // Confere todas as 4096 combinações de linhas, inclusive P2 dividido entre os dois registradores.
    void lines(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(path));
        for (unsigned first = 0; first < 64; ++first)
            for (unsigned second = 0; second < 64; ++second)
            {
                board->setInput({static_cast<std::uint16_t>(first), static_cast<std::uint16_t>(second)});
                unsigned even = 0xff, odd = 0xff;
                for (unsigned bit = 0; bit < 6; ++bit)
                    if (first & (1u << bit))
                        even &= ~(1u << bit);
                for (unsigned bit = 0; bit < 6; ++bit)
                    if (second & (1u << bit))
                    {
                        if (bit < 2)
                            even &= ~(1u << (6 + bit));
                        else
                            odd &= ~(1u << (bit - 2));
                    }
                check(board->input(0xdc) == even && board->input(0xdd) == odd, "Controller bit layout or active-low conversion");
            }
        board->setInput({mask(Button::Up) | mask(Button::A), mask(Button::Right) | mask(Button::B)});
        // As portas sem resposta implementada usam FF por convenção; leituras não acionam PSG ou VDP.
        const auto initialAddress = board->vdp().state().address;
        const auto initialLatch = board->psg().state().latch;
        for (unsigned port = 0; port < 0x80; ++port)
            for (unsigned high : {0u, 0xa500u})
                check(board->input(static_cast<std::uint16_t>(high | port)) == 0xff,
                      "Undriven and write-only ports must follow the current FF fallback");
        check(board->vdp().state().address == initialAddress && board->psg().state().latch == initialLatch,
              "Undriven reads must not access the VDP or PSG");
        // Escrever nas faixas não decodificadas não altera registradores ou sinais dos periféricos.
        for (unsigned port = 0; port < 0x100; ++port)
            if (port < 0x40 || port >= 0xc0)
                board->output(static_cast<std::uint16_t>(0xa500 | port), 0x90);
        check(board->vdp().state().address == initialAddress && !board->vdp().state().secondControlByte &&
                  board->psg().state().latch == initialLatch && !board->cpu().state().wait,
              "Writes to undecoded I/O must not alter the VDP, PSG or WAIT");
        for (unsigned port = 0xc0; port <= 0xff; ++port)
            check(board->input(static_cast<std::uint16_t>(0xa500 | port)) == (port & 1 ? 0xf5 : 0xee),
                  "Controller mirror or upper I/O address decoding");
        board->setButtons(0, mask(Button::Select));
        board->setButtons(1, 0);
        check(board->input(0xdc) == 0xff && board->input(0xdd) == 0xff && !board->cpu().state().nmiLine,
              "Select is not a hardware line on the SG-1000");
        R2SEGA8::Core::IO::Joypad joypad;
        joypad.setButtons(0, 0xff);
        joypad.setButtons(1, 0xff);
        joypad.reset();
        check(joypad.readEven() == 0xff && joypad.readOdd() == 0xff, "Joypad reset releases both players");
    }

    // Exercita IN e a NMI de Pause/Hold com CPU real, sem interromper o relógio do VDP.
    void cpu(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(path));
        board->setInput({mask(Button::Up) | mask(Button::A), mask(Button::Right) | mask(Button::B)});
        board->runCycles(256);
        check(board->readMemory(0xc000) == 0xee && board->readMemory(0xc001) == 0xf5 && board->readMemory(0xc002) == 1,
              "Z80 IN did not observe controller state");
        board->setInput({mask(Button::Start), 0});
        board->runCycles(256);
        check(board->readMemory(0xc010) == 1 && board->cpu().state().halted, "Start must trigger the NMI handler at 0066");
        for (unsigned i = 0; i < 3; ++i)
        {
            board->setInput({mask(Button::Start), 0});
            board->runCycles(256);
        }
        check(board->readMemory(0xc010) == 1, "Holding Start must not repeat NMI");
        board->setInput({0, mask(Button::Start)});
        board->runCycles(256);
        check(board->readMemory(0xc010) == 1, "Changing the held Start source must not create a spurious NMI edge");
        board->setInput({});
        board->setInput({0, mask(Button::Start)});
        board->runCycles(256);
        check(board->readMemory(0xc010) == 2, "Releasing and pressing Start must create a new NMI edge");
        check(board->vdp().state().masterTicks == board->cpu().state().cycles * 3, "Input must not add or stop emulated clocks");
    }

    // O turbo muda na fronteira do quadro emulado mesmo que o host não publique outra entrada.
    void turbo(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(path));
        board->setInput({mask(Button::TurboA), mask(Button::TurboB)});
        const auto thirdFrame = (2ULL * VDP::MasterTicksPerFrame + VDP::Height * VDP::MasterTicksPerLine) / 3;
        board->runCycles(thirdFrame - 1);
        check(board->videoFrame().number == 2 && (board->input(0xdc) & 0x10) && (board->input(0xdd) & 8),
              "Turbo must remain released before its emulated-frame boundary");
        board->runCycles(1);
        check(board->videoFrame().number == 3 && !(board->input(0xdc) & 0x10) && !(board->input(0xdd) & 8),
              "Turbo must pulse exactly at the third published frame");
        board->runCycles(VDP::MasterTicksPerFrame / 3);
        check((board->input(0xdc) & 0x10) && (board->input(0xdd) & 8), "Turbo pulse ends on the following emulated frame");
        board->setInput({mask(Button::A) | mask(Button::TurboA), 0});
        check(!(board->input(0xdc) & 0x10), "Normal button must prevail over a released turbo phase");
    }
} // namespace
int main()
{
    try
    {
        Fixture fixture;
        lines(fixture.path);
        cpu(fixture.path);
        turbo(fixture.path);
        std::cout << "SG-1000 controller lines, mirrors, CPU IN, Pause NMI and emulated turbo passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
