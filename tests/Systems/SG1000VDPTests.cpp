#include "Core/Systems/SG1000/SG1000.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Byte = std::uint8_t;
    using VDP = R2SEGA8::Core::TMS9918;
    using Board = R2SEGA8::Core::SG1000;

    // Interrompe a validação com uma descrição do comportamento esperado.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Gera uma ROM própria, sem assembler externo: inicializa Graphics I e conta IRQs em C000.
    std::vector<Byte> demoROM()
    {
        std::vector<Byte> rom(0x1400, 0);
        const Byte boot[] = {0xf3, 0x31, 0x00, 0xc4, 0xed, 0x56, 0xc3, 0x80, 0x00};
        std::copy(std::begin(boot), std::end(boot), rom.begin());
        const Byte isr[] = {0xf5, 0xdb, 0xbf, 0x3a, 0x00, 0xc0, 0x3c, 0x32, 0x00, 0xc0, 0xf1, 0xfb, 0xed, 0x4d};
        std::copy(std::begin(isr), std::end(isr), rom.begin() + 0x38);
        std::size_t pc = 0x80;
        auto emit = [&](std::initializer_list<Byte> code)
        {
            for (auto value : code)
                rom.at(pc++) = value;
        };
        auto out = [&](Byte value, Byte port)
        { emit({0x3e, value, 0xd3, port}); };
        auto reg = [&](Byte index, Byte value)
        {
            out(value, 0xbf);
            out(static_cast<Byte>(0x80 | index), 0xbf);
        };
        auto address = [&](unsigned location)
        {
            out(static_cast<Byte>(location), 0xbf);
            out(static_cast<Byte>((location >> 8) | 0x40), 0xbf);
        };
        emit({0xaf, 0x32, 0x00, 0xc0});
        reg(0, 0);
        reg(1, 0x80);
        reg(2, 6);
        reg(3, 0x80);
        reg(4, 0);
        reg(5, 0x36);
        reg(6, 0);
        reg(7, 4);
        address(0x1800);
        emit({0x21, 0x00, 0x10, 0x01, 0xbe, 0x00, 0xed, 0xb3, 0x06, 0x00, 0xed, 0xb3, 0x06, 0x00, 0xed, 0xb3});
        address(0);
        emit({0x21, 0x00, 0x13, 0x01, 0xbe, 72, 0xed, 0xb3});
        address(0x2000);
        emit({0x21, 0x48, 0x13, 0x01, 0xbe, 2, 0xed, 0xb3});
        address(0x1b00);
        out(0xd0, 0xbe);    // Encerra a tabela para isolar esta demonstração de background.
        emit({0xdb, 0xbf}); // Limpa uma possível flag de quadro acumulada durante a inicialização.
        reg(1, 0xe0);
        emit({0xfb, 0x76, 0x18, 0xfd});
        check(pc < 0x1000, "Demo code overlaps its ROM data");
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 32; ++x)
                rom[0x1000 + y * 32 + x] = (x + y) % 2 ? 8 : 0;
        for (unsigned y = 0; y < 8; ++y)
        {
            rom[0x1300 + y] = static_cast<Byte>(0x80 >> y);
            rom[0x1340 + y] = 0xff;
        }
        rom[0x1348] = 0x20;
        rom[0x1349] = 0xf1;
        return rom;
    }

    // Mantém uma ROM temporária exclusiva e remove somente o arquivo criado pelo teste.
    struct Fixture
    {
        std::filesystem::path path;
        // Grava o programa de integração no formato aceito pelo cartucho.
        explicit Fixture(const std::vector<Byte> &rom)
        {
            path = std::filesystem::temp_directory_path() /
                   ("r2sega8-vdp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sg");
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()), static_cast<std::streamsize>(rom.size()));
            file.close();
            check(bool(file), "Cannot write integration ROM");
        }
        // Libera o arquivo sem ocultar uma falha anterior por uma exceção de limpeza.
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };

    // Programa o VDP usando as portas da placa, exercitando também a atualização da linha INT.
    void reg(Board &board, Byte index, Byte value)
    {
        board.output(0xbf, value);
        board.output(0xbf, static_cast<Byte>(0x80 | index));
    }

    // Confere todos os espelhos de porta e a relação exata entre o clock da CPU e o raster.
    void mappingAndClock(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(path));
        auto halted = board->cpu().state();
        halted.halted = true;
        board->cpu().setState(halted);
        for (unsigned port = 0x80; port < 0xc0; port += 2)
        {
            board->output(static_cast<std::uint16_t>(0xa500 | (port + 1)), 0x34);
            board->output(static_cast<std::uint16_t>(0x5a00 | (port + 1)), 0x52);
            board->output(static_cast<std::uint16_t>(0x1200 | port), static_cast<Byte>(port));
            board->runCycles(32);
            check(board->vdp().state().vram[0x1234] == port, "Mirrored VDP writes ignore upper I/O address bits");
            board->output(0xbf, 0x34);
            board->output(0x81, 0x12);
            board->runCycles(32);
            check(board->input(static_cast<std::uint16_t>(0xfe00 | port)) == port, "Mirrored VDP data reads");
        }
        const auto address = board->vdp().state().address;
        board->output(0x7f, 0x99);
        board->output(0xc1, 0x55);
        check(board->input(0xdc) == 0xff && board->input(0x7e) == 0xff && board->vdp().state().address == address,
              "Unimplemented devices do not alias VDP ports");
        board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(path));
        auto s = board->cpu().state();
        s.halted = true;
        board->cpu().setState(s);
        reg(*board, 1, 0xa0);
        constexpr auto activeCPUTicks = VDP::Height * VDP::MasterTicksPerLine / 3;
        board->runCycles(activeCPUTicks - 1);
        check(!board->cpu().state().irq && board->vdp().state().masterTicks == 3ULL * (activeCPUTicks - 1),
              "VDP runs during HALT with exact 3:1 crystal/CPU clock ratio");
        board->runCycles(1);
        check(board->cpu().state().irq && board->vdp().state().line == 192 && board->vdp().state().dot == 0,
              "VBlank reaches CPU INT at the exact boundary");
        reg(*board, 1, 0x80);
        check(!board->cpu().state().irq, "Register write disables INT immediately");
        reg(*board, 1, 0xa0);
        check(board->cpu().state().irq, "Register write enables a pending INT immediately");
        check((board->input(0xa581) & 0x80) && !board->cpu().state().irq, "Status read acknowledges CPU INT immediately");
        board->cpu().setWAIT(true);
        board->runCycles(100);
        check(board->vdp().state().masterTicks == board->cpu().state().cycles * 3, "WAIT does not stop the VDP clock");
        board->cpu().setWAIT(false);
    }

    // Verifica a imagem completa produzida por instruções OUT e a rotina de interrupção emulada.
    void validateDemo(Board &board)
    {
        board.runCycles(200000);
        const auto frame = board.videoFrame();
        check(frame.number >= 3 && board.readMemory(0xc000) == frame.number && board.cpu().state().halted,
              "Z80 demo did not configure video and acknowledge every VBlank via its ISR");
        check(!board.cpu().state().irq && board.vdp().state().masterTicks == 600000, "IRQ acknowledgement or clock ratio diverged");
        check(board.vdp().state().registers[1] == 0xe0 && board.vdp().state().vram[0x1b00] == 0xd0,
              "Demo register/VRAM writes did not reach VDP");
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const bool white = (x / 8 + y / 8) % 2 != 0;
                const bool green = !white && x % 8 == y % 8;
                const std::array<Byte, 4> expected = white   ? std::array<Byte, 4>{255, 255, 255, 255}
                                                     : green ? std::array<Byte, 4>{33, 200, 66, 255}
                                                             : std::array<Byte, 4>{84, 85, 237, 255};
                for (unsigned c = 0; c < 4; ++c)
                    check(frame.rgba[(y * 256 + x) * 4 + c] == expected[c], "CPU-driven Graphics I frame differs from expected pixels");
            }
    }

    // Compara uma transferência OTIR rápida demais com uma rotina espaçada, executadas pelo Z80 real.
    void transferPacing()
    {
        for (bool safe : {false, true})
        {
            std::vector<Byte> rom(0x200, 0);
            std::size_t pc = 0;
            auto emit = [&](std::initializer_list<Byte> code)
            {
                for (auto byte : code)
                    rom.at(pc++) = byte;
            };
            emit({0xf3, 0x31, 0, 0xc4, 0x3e, 0xc0, 0xd3, 0xbf, 0x3e, 0x81, 0xd3, 0xbf});
            emit({0x3e, 0, 0xd3, 0xbf, 0x3e, 0x78, 0xd3, 0xbf}); // Endereço 3800, com imagem habilitada.
            emit({0x21, 0, 1, 0x01, 0xbe, 64});
            const auto loop = static_cast<Byte>(pc);
            if (safe)
                emit({0xed, 0xa3, 0x00, 0xc2, loop, 0}); // OUTI + NOP + JP NZ: 30 T-states entre bytes.
            else
                emit({0xed, 0xb3}); // OTIR: 21 T-states entre bytes, insuficientes em algumas fases.
            for (unsigned i = 0; i < 32; ++i)
                emit({0});
            emit({0x3e, 1, 0x32, 1, 0xc0, 0x76, 0x18, 0xfd});
            for (unsigned i = 0; i < 64; ++i)
                rom[0x100 + i] = static_cast<Byte>(i + 1);
            Fixture fixture(rom);
            auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(fixture.path));
            for (unsigned elapsed = 0; elapsed < 20000 && board->readMemory(0xc001) != 1; elapsed += 128)
                board->runCycles(128);
            check(board->readMemory(0xc001) == 1, "CPU transfer pacing ROM did not complete");
            const auto &vram = board->vdp().state().vram;
            if (safe)
            {
                for (unsigned i = 0; i < 64; ++i)
                    check(vram[0x3800 + i] == i + 1, "Safely paced Z80 writes must all reach VRAM");
            }
            else
            {
                unsigned stored = 0;
                while (stored < 64 && vram[0x3800 + stored])
                    ++stored;
                check(stored > 0 && stored < 64, "Too-fast Z80 writes must lose requests instead of bypassing arbitration");
            }
            check(board->vdp().state().masterTicks == board->cpu().state().cycles * 3,
                  "VRAM contention must not invent CPU clocks or automatic WAIT states");
        }
    }

    // Confere os pixels anteriores e posteriores a duas escritas de R7 feitas pela CPU durante a imagem.
    void rasterColors()
    {
        std::vector<Byte> rom(0x100, 0);
        const Byte code[] = {0xf3, 0x31, 0, 0xc4, 0x3e, 4, 0xd3, 0xbf, 0x3e, 0x87, 0xd3, 0xbf, 0x06, 72,
                             0x10, 0xfe, 0x3e, 6, 0xd3, 0xbf, 0x3e, 0x87, 0xd3, 0xbf, 0x76, 0x18, 0xfd};
        std::copy(std::begin(code), std::end(code), rom.begin());
        Fixture fixture(rom);
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(fixture.path));
        std::array<unsigned, 2> changes{};
        unsigned count = 0;
        for (unsigned ticks = 0; ticks < 50000 && board->videoFrame().number == 0; ++ticks)
        {
            const auto &before = board->vdp().state();
            const unsigned position = before.line * VDP::DotsPerLine + before.dot;
            const auto color = before.registers[7];
            board->runCycles(1);
            if (board->vdp().state().registers[7] != color)
            {
                check(count < changes.size() && position % VDP::DotsPerLine < VDP::Width,
                      "Raster ROM changed color outside its expected visible dot");
                changes[count++] = position;
            }
        }
        check(count == 2 && board->videoFrame().number == 1, "Raster ROM did not produce both register changes and a complete frame");
        const auto frame = board->videoFrame();
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const unsigned position = y * VDP::DotsPerLine + x;
                const std::array<Byte, 3> expected = position < changes[0]   ? std::array<Byte, 3>{0, 0, 0}
                                                     : position < changes[1] ? std::array<Byte, 3>{84, 85, 237}
                                                                             : std::array<Byte, 3>{212, 82, 77};
                const auto *pixel = frame.rgba + y * frame.pitch + x * 4;
                check(pixel[0] == expected[0] && pixel[1] == expected[1] && pixel[2] == expected[2] && pixel[3] == 255,
                      "CPU register write must affect the next raster dot without redrawing earlier pixels");
            }
    }

    // Exporta a ROM própria e a imagem observada para revisão manual no emulador.
    void exportDemo(const std::filesystem::path &directory, const std::vector<Byte> &rom, const Board &board,
                    const std::string &name = "graphics-i")
    {
        std::filesystem::create_directories(directory);
        std::ofstream file(directory / (name + "-demo.sg"), std::ios::binary);
        file.write(reinterpret_cast<const char *>(rom.data()), static_cast<std::streamsize>(rom.size()));
        file.close();
        check(bool(file), "Cannot export demo ROM");
        const auto frame = board.videoFrame();
        std::ofstream image(directory / (name + "-demo.ppm"), std::ios::binary);
        image << "P6\n256 192\n255\n";
        for (unsigned i = 0; i < 256 * 192; ++i)
            image.write(reinterpret_cast<const char *>(frame.rgba + i * 4), 3);
        image.close();
        check(bool(image), "Cannot export demo frame");
    }

    // Cria uma ROM que envia 16 KiB de dados ao VDP enquanto a tela está apagada.
    std::vector<Byte> backgroundROM(unsigned mode)
    {
        std::array<Byte, 0x4000> vram{};
        constexpr Byte glyph[8] = {0x78, 0x84, 0x84, 0xfc, 0x84, 0x84, 0x84, 0};
        constexpr Byte blocks[8] = {0x20, 0xf1, 0x64, 0x82, 0xa3, 0x5e, 0xc7, 0x9d};
        if (mode == 2)
        {
            constexpr Byte patterns[3] = {0xf0, 0xaa, 0x81};
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned row = 0; row < 8; ++row)
                {
                    vram[bank * 0x800 + row] = patterns[bank];
                    vram[0x2000 + bank * 0x800 + row] = static_cast<Byte>(((row + 2 + bank * 4) % 16 << 4) | ((bank + row) % 16));
                }
        }
        else
            std::copy_n(mode == 1 ? glyph : blocks, 8, vram.begin());
        vram[0x3b00] = 0xd0;
        if (mode == 8)
        {
            vram.fill(0);
            // Trinta e dois sprites 16x16 ampliados: oito por faixa, somente quatro visíveis por linha.
            constexpr Byte spriteColors[] = {2, 15, 6, 10, 3, 8, 12, 14};
            for (unsigned index = 0; index < 32; ++index)
            {
                const unsigned entry = 0x3b00 + index * 4;
                vram[entry] = static_cast<Byte>(31 + (index / 8) * 40);
                vram[entry + 1] = static_cast<Byte>(16 + (index % 8) * 28);
                vram[entry + 2] = 3; // SIZE=1 deve selecionar o padrão alinhado zero.
                vram[entry + 3] = spriteColors[index % 8];
            }
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                    if (x == 0 || x == 15 || y == 0 || y == 15 || x == y || x + y == 15)
                        vram[0x2800 + (x / 8) * 16 + y] |= static_cast<Byte>(0x80 >> (x % 8));
        }
        std::vector<Byte> rom(0x5000, 0);
        const Byte boot[] = {0xf3, 0x31, 0, 0xc4, 0xed, 0x56, 0xc3, 0x80, 0};
        const Byte isr[] = {0xf5, 0xdb, 0xbf, 0x32, 2, 0xc0, 0x3a, 0, 0xc0, 0x3c, 0x32, 0, 0xc0, 0xf1, 0xfb, 0xed, 0x4d};
        std::copy(std::begin(boot), std::end(boot), rom.begin());
        std::copy(std::begin(isr), std::end(isr), rom.begin() + 0x38);
        std::size_t pc = 0x80;
        auto emit = [&](std::initializer_list<Byte> code)
        {
            for (auto byte : code)
                rom.at(pc++) = byte;
        };
        auto out = [&](Byte value)
        { emit({0x3e, value, 0xd3, 0xbf}); };
        auto setRegister = [&](Byte index, Byte value)
        {
            out(value);
            out(static_cast<Byte>(0x80 | index));
        };
        emit({0xaf, 0x32, 0, 0xc0, 0x32, 1, 0xc0});
        setRegister(0, mode == 2 ? 2 : 0);
        setRegister(1, 0x80);
        setRegister(2, 6);
        setRegister(3, mode == 2 ? 0xff : 0x80);
        setRegister(4, mode == 2 ? 3 : 0);
        setRegister(5, 0x76);
        setRegister(6, mode == 8 ? 5 : 0);
        setRegister(7, mode == 1 ? 0xf4 : 4);
        out(0);
        out(0x40);
        emit({0x21, 0, 0x10, 0x0e, 0xbe, 0x16, 64, 0x06, 0, 0xed, 0xb3, 0x15, 0x20, 0xf9});
        emit({0xdb, 0xbf});
        setRegister(1, mode == 1 ? 0xf0 : mode == 4 ? 0xe8
                                      : mode == 8   ? 0xe3
                                                    : 0xe0);
        emit({0x3e, 1, 0x32, 1, 0xc0, 0xfb, 0x76, 0x18, 0xfd});
        check(pc < 0x1000, "Background ROM code overlaps video data");
        std::copy(vram.begin(), vram.end(), rom.begin() + 0x1000);
        return rom;
    }

    // Verifica o quadro e o atendimento de VBlank depois da inicialização feita pela própria CPU.
    void validateBackground(Board &board, unsigned mode)
    {
        for (unsigned elapsed = 0; elapsed < 600000 && board.readMemory(0xc001) != 1; elapsed += 256)
            board.runCycles(256);
        check(board.readMemory(0xc001) == 1, "Z80 did not finish background initialization");
        board.runCycles(2048);
        const auto initialFrame = board.videoFrame().number;
        const auto initialIRQ = board.readMemory(0xc000);
        board.runCycles((VDP::MasterTicksPerFrame / 3) * 3);
        check(board.videoFrame().number == initialFrame + 3 && static_cast<Byte>(board.readMemory(0xc000) - initialIRQ) == 3 &&
                  board.cpu().state().halted && !board.cpu().state().irq,
              "Background mode broke CPU/VBlank synchronization");
        if (mode == 8)
            check(board.readMemory(0xc002) == 0xe4, "Z80 ISR did not observe and acknowledge sprite collision/overflow/VBlank");
        constexpr Byte RGB[16][3] = {{0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120}, {84, 85, 237}, {125, 118, 252}, {212, 82, 77}, {66, 235, 245}, {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128}, {33, 176, 59}, {201, 91, 186}, {204, 204, 204}, {255, 255, 255}};
        constexpr Byte glyph[8] = {0x78, 0x84, 0x84, 0xfc, 0x84, 0x84, 0x84, 0};
        constexpr Byte blocks[8] = {0x20, 0xf1, 0x64, 0x82, 0xa3, 0x5e, 0xc7, 0x9d};
        const auto frame = board.videoFrame();
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                unsigned color = 4;
                if (mode == 2)
                {
                    const unsigned bank = y / 64, row = y % 8;
                    const bool bit = bank == 0 ? x % 8 < 4 : bank == 1 ? x % 2 == 0
                                                                       : x % 8 == 0 || x % 8 == 7;
                    color = bit ? (row + 2 + bank * 4) % 16 : (bank + row) % 16;
                }
                else if (mode == 1)
                {
                    if (x >= 6 && x < 246 && (glyph[y % 8] & (0x80 >> ((x - 6) % 6))))
                        color = 15;
                }
                else if (mode == 8)
                {
                    constexpr unsigned spriteColors[] = {2, 15, 6, 10};
                    if (y >= 32 && y < 184 && (y - 32) % 40 < 32)
                        for (unsigned column = 0; column < 4; ++column)
                        {
                            const unsigned left = 16 + column * 28;
                            if (x < left || x >= left + 32)
                                continue;
                            const unsigned u = (x - left) / 2, v = ((y - 32) % 40) / 2;
                            if (u == 0 || u == 15 || v == 0 || v == 15 || u == v || u + v == 15)
                            {
                                color = spriteColors[column];
                                break;
                            }
                        }
                }
                else
                {
                    const auto pair = blocks[(y / 4) % 8];
                    color = x % 8 < 4 ? pair >> 4 : pair & 15;
                }
                if (!color)
                    color = 4;
                const auto *pixel = frame.rgba + y * frame.pitch + x * 4;
                check(pixel[0] == RGB[color][0] && pixel[1] == RGB[color][1] && pixel[2] == RGB[color][2] && pixel[3] == 255,
                      "CPU-driven background frame differs from expected pixels");
            }
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto rom = demoROM();
        Fixture fixture(rom);
        mappingAndClock(fixture.path);
        transferPacing();
        rasterColors();
        auto board = std::make_unique<Board>(R2SEGA8::Core::Cartridge(fixture.path));
        validateDemo(*board);
        if (argc == 2)
            exportDemo(std::filesystem::u8path(argv[1]), rom, *board);
        for (unsigned mode : {2u, 1u, 4u, 8u})
        {
            const auto modeROM = backgroundROM(mode);
            Fixture modeFixture(modeROM);
            auto modeBoard = std::make_unique<Board>(R2SEGA8::Core::Cartridge(modeFixture.path));
            validateBackground(*modeBoard, mode);
            if (argc == 2)
                exportDemo(std::filesystem::u8path(argv[1]), modeROM, *modeBoard,
                           mode == 2   ? "graphics-ii"
                           : mode == 1 ? "text"
                           : mode == 8 ? "sprites"
                                       : "multicolor");
        }
        std::cout << "SG-1000 ports, CPU/VDP clock, INT and Z80 demos for all four backgrounds and sprites passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}