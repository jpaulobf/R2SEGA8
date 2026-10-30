#include "Core/VDP/TMS9918/TMS9918.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;

    // Encerra o teste com o contexto da propriedade que falhou.
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Programa um endereço pela mesma sequência de dois bytes usada pela CPU.
    void address(VDP &vdp, unsigned value, bool writing)
    {
        vdp.writeControl(static_cast<std::uint8_t>(value));
        vdp.writeControl(static_cast<std::uint8_t>((value >> 8) | (writing ? 0x40 : 0)));
    }

    // Escreve um registrador através da porta de controle, sem alterar o estado diretamente.
    void reg(VDP &vdp, unsigned index, unsigned value)
    {
        vdp.writeControl(static_cast<std::uint8_t>(value));
        vdp.writeControl(static_cast<std::uint8_t>(0x80 | index));
    }

    // Aguarda o slot real sem adicionar clocks à implementação das portas.
    void flush(VDP &vdp)
    {
        unsigned ticks = 0;
        while (vdp.state().cpuAccessPending && ticks++ < 100)
            vdp.tick();
        check(!vdp.state().cpuAccessPending, "Pending VRAM access did not receive a slot");
    }

    // Envia um byte e aguarda a execução física da escrita.
    void write(VDP &vdp, std::uint8_t value)
    {
        vdp.writeData(value);
        flush(vdp);
    }

    // Retorna o buffer anterior e aguarda a próxima pré-busca.
    std::uint8_t read(VDP &vdp)
    {
        const auto value = vdp.readData();
        flush(vdp);
        return value;
    }

    // Verifica o protocolo da CPU, incremento, pré-busca, cancelamento e reset.
    void ports()
    {
        auto storage = std::make_unique<VDP>();
        auto &vdp = *storage;
        check(vdp.state().vram[0] == 0 && !vdp.interrupt(), "Initial VRAM/IRQ");
        for (unsigned i = 0; i < 8; ++i)
        {
            reg(vdp, i, i * 29);
            check(vdp.state().registers[i] == i * 29, "Register write protocol");
        }
        address(vdp, 0x3fff, true);
        write(vdp, 0xa5);
        write(vdp, 0x5a);
        check(vdp.state().address == 1 && vdp.state().vram[0x3fff] == 0xa5 && vdp.state().vram[0] == 0x5a,
              "14-bit VRAM address wraps during writes");
        address(vdp, 0x3fff, false);
        flush(vdp);
        check(vdp.state().address == 0 && vdp.state().readBuffer == 0xa5, "Read setup performs prefetch");
        check(read(vdp) == 0xa5 && vdp.state().address == 1 && read(vdp) == 0x5a,
              "Read buffer and sequential prefetch wrap");
        address(vdp, 0x2000, true);
        write(vdp, 0x91);
        check(read(vdp) == 0x91, "Data write updates the CPU-visible read buffer");

        vdp.writeControl(0x33);
        vdp.readStatus();
        reg(vdp, 7, 0x04);
        check(vdp.state().registers[7] == 4 && !vdp.state().secondControlByte, "Status cancels half a control word");
        address(vdp, 0x1200, true);
        vdp.writeControl(0x45);
        write(vdp, 0x82);
        check(vdp.state().vram[0x1245] == 0x82 && !vdp.state().secondControlByte,
              "First control byte changes address low byte; data write cancels control latch");
        vdp.writeControl(0x67);
        read(vdp);
        reg(vdp, 6, 3);
        check(vdp.state().registers[6] == 3, "Data read cancels half a control word");
        vdp.reset();
        check(vdp.state().vram[0x1245] == 0x82 && vdp.state().registers[0] == 0 && vdp.state().registers[1] == 0 &&
                  !vdp.state().secondControlByte && vdp.state().masterTicks == 0 && !vdp.interrupt(),
              "Reset preserves VRAM and resets controller");
        address(vdp, 0, true);
        for (unsigned i = 0; i < 0x4000; ++i)
            write(vdp, static_cast<std::uint8_t>(i * 13 + i / 256));
        address(vdp, 0, false);
        flush(vdp);
        for (unsigned i = 0; i < 0x4000; ++i)
            check(read(vdp) == static_cast<std::uint8_t>(i * 13 + i / 256), "Complete 16 KiB sequential port round-trip");
    }

    // Confere a fronteira exata do VBlank, o nível de INT e a publicação de quadros.
    void timingAndIRQ()
    {
        auto storage = std::make_unique<VDP>();
        auto &vdp = *storage;
        constexpr auto activeTicks = VDP::Height * VDP::MasterTicksPerLine;
        vdp.tick();
        check(vdp.state().dot == 0 && vdp.state().masterPhase == 1, "First crystal tick does not advance a pixel");
        vdp.tick();
        check(vdp.state().dot == 1 && vdp.state().masterPhase == 0, "Pixel clock is crystal divided by two");
        vdp.runMasterTicks(activeTicks - 3);
        check(vdp.state().line == 191 && vdp.state().dot == 341 && !(vdp.state().status & 0x80) && vdp.frame().number == 0,
              "VBlank must not arrive one master tick early");
        vdp.tick();
        check(vdp.state().line == 192 && vdp.state().dot == 0 && (vdp.state().status & 0x80) && !vdp.interrupt() && vdp.frame().number == 1,
              "VBlank latches even with display and interrupt disabled");
        reg(vdp, 1, 0xa0);
        check(vdp.interrupt(), "Enabling IE with pending VBlank asserts INT immediately");
        reg(vdp, 1, 0x80);
        check(!vdp.interrupt() && (vdp.state().status & 0x80), "Disabling IE preserves pending frame flag");
        reg(vdp, 1, 0xa0);
        check((vdp.readStatus() & 0x80) && !vdp.interrupt() && !(vdp.readStatus() & 0x80), "Reading status acknowledges INT once");
        vdp.runMasterTicks(VDP::MasterTicksPerFrame - activeTicks);
        check(vdp.state().line == 0 && vdp.state().dot == 0 && vdp.state().masterPhase == 0, "Exact NTSC frame length");
        vdp.runMasterTicks(activeTicks);
        check(vdp.interrupt() && vdp.frame().number == 2, "Next frame produces a new interrupt");
        vdp.readStatus();
        vdp.runMasterTicks(VDP::MasterTicksPerFrame * 3ULL);
        check(vdp.interrupt() && vdp.frame().number == 5, "Unacknowledged INT stays asserted across frames");
    }

    // Verifica cores, orientação dos bits, bordas de tiles e estabilidade do quadro publicado.
    void graphicsI()
    {
        auto storage = std::make_unique<VDP>();
        auto &vdp = *storage;
        reg(vdp, 1, 0xc0);
        reg(vdp, 2, 6);
        reg(vdp, 3, 0x80);
        reg(vdp, 4, 1);
        reg(vdp, 7, 4);
        reg(vdp, 5, 0x7f);
        address(vdp, 0x3f80, true);
        write(vdp, 0xd0);
        address(vdp, 0x1800, true);
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 32; ++x)
                write(vdp, static_cast<std::uint8_t>((x + y) % 2 ? 8 : 0));
        address(vdp, 0x0800, true);
        for (unsigned row = 0; row < 8; ++row)
            write(vdp, static_cast<std::uint8_t>(0x80 >> row));
        address(vdp, 0x0840, true);
        for (unsigned row = 0; row < 8; ++row)
            write(vdp, 0xff);
        address(vdp, 0x2000, true);
        write(vdp, 0x20);
        write(vdp, 0xf1);
        while (vdp.state().line || vdp.state().dot || vdp.state().masterPhase)
            vdp.tick();
        vdp.runMasterTicks(VDP::MasterTicksPerFrame);
        const auto before = vdp.frame();
        check(before.rgba[3] == 255, "Published image is opaque");
        vdp.runMasterTicks(VDP::Height * VDP::MasterTicksPerLine);
        const auto frame = vdp.frame();
        check(frame.width == 256 && frame.height == 192 && frame.pitch == 1024 && frame.number == before.number + 1, "Presentation frame metadata");
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const bool white = ((x / 8 + y / 8) % 2) != 0;
                const bool green = !white && x % 8 == y % 8;
                const std::array<std::uint8_t, 4> expected = white ? std::array<std::uint8_t, 4>{255, 255, 255, 255} : green ? std::array<std::uint8_t, 4>{33, 200, 66, 255}
                                                                                                                             : std::array<std::uint8_t, 4>{84, 85, 237, 255};
                for (unsigned c = 0; c < 4; ++c)
                    check(frame.rgba[(y * 256 + x) * 4 + c] == expected[c], "Graphics I name/pattern/color/backdrop pixel mismatch");
            }
        const auto saved = std::make_unique<VDP::State>(vdp.state());
        reg(vdp, 1, 0x80);
        reg(vdp, 7, 6);
        vdp.runMasterTicks(VDP::MasterTicksPerFrame - 1);
        check(vdp.frame().number == frame.number && vdp.frame().rgba[0] == 33, "Front image remains stable until publication");
        vdp.tick();
        check(vdp.frame().number == frame.number + 1 && vdp.frame().rgba[0] == 212 && vdp.frame().rgba[1] == 82 && vdp.frame().rgba[2] == 77,
              "Display disable renders backdrop while timing continues");
        vdp.restore(*saved);
        check(vdp.frame().number == frame.number && vdp.frame().rgba[0] == 33, "Restore includes front/back images");
        reg(vdp, 0, 2);
        reg(vdp, 1, 0xd0);
        reg(vdp, 7, 6);
        vdp.runMasterTicks(VDP::MasterTicksPerFrame);
        check(vdp.frame().rgba[0] == 212, "Undocumented mode combinations do not silently render as Graphics I");
        saved->address = 0x4000;
        bool rejected = false;
        try
        {
            vdp.restore(*saved);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid restored VRAM address rejected");
    }
}

int main()
{
    try
    {
        ports();
        timingAndIRQ();
        graphicsI();
        std::cout << "TMS9918 ports, prefetch, latches, NTSC timing, IRQ, Graphics I and restore passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}