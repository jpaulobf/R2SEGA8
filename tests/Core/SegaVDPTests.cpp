#include "Core/VDP/SegaVDP/SegaVDP.h"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using R2SEGA8::Core::SegaVDP;

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void command(SegaVDP &vdp, unsigned address, unsigned code)
    {
        vdp.writeControl(static_cast<std::uint8_t>(address));
        vdp.writeControl(static_cast<std::uint8_t>((code << 6) | ((address >> 8) & 0x3f)));
    }

    void reg(SegaVDP &vdp, unsigned index, unsigned value)
    {
        vdp.writeControl(static_cast<std::uint8_t>(value));
        vdp.writeControl(static_cast<std::uint8_t>(0x80 | index));
    }

    void write(SegaVDP &vdp, unsigned address, unsigned code, std::initializer_list<std::uint8_t> bytes)
    {
        command(vdp, address, code);
        for (auto value : bytes)
            vdp.writeData(value);
    }

    void normalTables(SegaVDP &vdp)
    {
        reg(vdp, 2, 0x0f);
        reg(vdp, 3, 0xff);
        reg(vdp, 4, 0x07);
        reg(vdp, 5, 0x7f);
        reg(vdp, 6, 0x07);
    }

    void pixel(const SegaVDP &vdp, unsigned x, unsigned y, unsigned r, unsigned g, unsigned b)
    {
        const auto frame = vdp.frame();
        const auto *p = frame.rgba + frame.pitch * y + 4 * x;
        if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255)
            std::cerr << "Pixel (" << x << "," << y << "): got " << unsigned(p[0]) << "," << unsigned(p[1])
                      << "," << unsigned(p[2]) << "; expected " << r << "," << g << "," << b << '\n';
        check(p[0] == r && p[1] == g && p[2] == b && p[3] == 255, "Mode 4 pixel mismatch");
    }

    void rasterAndBackground()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        reg(vdp, 7, 2);
        // Pattern 257: pixel (0,0) is color 5; (7,7) is color 2.
        write(vdp, 257 * 32, 1, {0x80, 0, 0x80, 0});
        write(vdp, 257 * 32 + 28, 1, {0, 1, 0, 0});
        // First tile uses palette 0; the second flips both axes and selects palette 1.
        write(vdp, 0x3800, 1, {1, 1, 1, 0x0f});
        write(vdp, 5, 3, {0x03});
        write(vdp, 0x12, 3, {0x0c});
        write(vdp, 0x15, 3, {0x30});
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2 - 1);
        check(vdp.frame().number == 0 && !(vdp.state().status & 0x80), "VBlank arrived early");
        vdp.runMasterTicks(1);
        check(vdp.frame().number == 1 && !(vdp.state().status & 0x80), "Frame publication precedes VBlank IRQ");
        pixel(vdp, 0, 0, 255, 0, 0);
        pixel(vdp, 8, 0, 0, 255, 0);
        pixel(vdp, 15, 7, 0, 0, 255);
        vdp.runMasterTicks(316 * 2);
        check(!(vdp.state().status & 0x80) && !vdp.interrupt(), "VBlank must not latch on line C0");
        vdp.runMasterTicks(2);
        check(vdp.state().frameInterruptPending && !(vdp.state().status & 0x80) && !vdp.interrupt(),
              "Frame interrupt must become pending before its status flag");
        reg(vdp, 1, 0x60);
        check(vdp.interrupt(), "Enabling pending frame IRQ must assert IRQ");
        check(vdp.verticalCounter() == 0xc1 && !(vdp.state().status & 0x80),
              "V counter changes before the VBlank status flag");
        vdp.runMasterTicks(2);
        check((vdp.state().status & 0x80) && vdp.interrupt(), "VBlank flag must latch on line C1");
        check((vdp.readStatus() & 0x80) && !vdp.interrupt(), "Status read must acknowledge IRQ");
        vdp.setRenderTiles(false);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 0, 255, 0);
        reg(vdp, 1, 0x20);
        vdp.setRenderTiles(true);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 0, 255, 0);
    }

    void scrolling()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        // Quatro padrões sólidos distinguem as paridades da linha e da coluna do mapa.
        for (unsigned tile = 1; tile <= 4; ++tile)
        {
            command(vdp, tile * 32, 1);
            for (unsigned row = 0; row < 8; ++row)
                for (unsigned plane = 0; plane < 4; ++plane)
                    vdp.writeData((tile & (1u << plane)) ? 0xff : 0);
        }
        command(vdp, 0x3800, 1);
        for (unsigned row = 0; row < 28; ++row)
            for (unsigned col = 0; col < 32; ++col)
            {
                vdp.writeData(static_cast<std::uint8_t>(1 + (col & 1) + 2 * (row & 1)));
                vdp.writeData(0);
            }
        write(vdp, 1, 3, {0x03, 0x0c, 0x30, 0x3f});
        write(vdp, 16, 3, {0x15});
        reg(vdp, 8, 3);
        reg(vdp, 9, 223);
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2);
        pixel(vdp, 0, 0, 0, 0, 0); // The fine-scroll gap uses background palette color zero.
        pixel(vdp, 3, 0, 0, 0, 255);
        pixel(vdp, 3, 1, 255, 0, 0); // y wraps from 223 to 0.
        reg(vdp, 0, 0xc4);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 0, 0, 255);   // Top rows ignore horizontal scroll.
        pixel(vdp, 192, 0, 255, 0, 0); // Right columns ignore vertical scroll.
        pixel(vdp, 3, 16, 0, 0, 255);  // Horizontal scroll resumes below line 15.
        reg(vdp, 0, 0x24);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 85, 85, 85); // Left-column blank uses backdrop.
        pixel(vdp, 8, 0, 0, 0, 255);

        // R9 is captured at frame start, whereas R8 is captured for each scanline.
        reg(vdp, 0, 4);
        reg(vdp, 8, 0);
        reg(vdp, 9, 0);
        vdp.runMasterTicks((SegaVDP::LinesPerFrame - SegaVDP::Height) * SegaVDP::DotsPerLine * 2 + 2);
        reg(vdp, 8, 8);
        reg(vdp, 9, 8);
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2 - 2);
        pixel(vdp, 0, 0, 255, 0, 0);
        pixel(vdp, 0, 1, 0, 255, 0);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 255, 255, 255);
    }

    void fineScrollEdge()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 6, 3);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        // Red priority tiles and a blue background color zero; backdrop is green.
        command(vdp, 32, 1);
        for (unsigned row = 0; row < 8; ++row)
            for (auto value : {0xff, 0, 0, 0})
                vdp.writeData(static_cast<std::uint8_t>(value));
        command(vdp, 0x3800, 1);
        for (unsigned i = 0; i < 32 * 28; ++i)
        {
            vdp.writeData(1);
            vdp.writeData(0x10);
        }
        write(vdp, 0, 3, {0x30, 0x03});
        write(vdp, 16, 3, {0x0c, 0, 0x3f});
        // A white sprite at x=0 checks that the gap has no background priority.
        write(vdp, 64, 1, {0, 0x80, 0, 0});
        write(vdp, 0x3f00, 1, {0xff, 0xd0});
        write(vdp, 0x3f80, 1, {0, 2});
        // Altera R8 no VBlank, antes da captura para a primeira linha.
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2);
        for (unsigned fine = 0; fine < 8; ++fine)
        {
            reg(vdp, 8, 8 + fine);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            if (fine == 0)
                pixel(vdp, 0, 0, 255, 0, 0);
            else
            {
                pixel(vdp, 0, 0, 255, 255, 255);
                for (unsigned x = 1; x < fine; ++x)
                    pixel(vdp, x, 0, 0, 0, 255);
                pixel(vdp, fine, 0, 255, 0, 0);
            }
        }
        reg(vdp, 0, 0x44); // Horizontal scroll is locked on the first 16 lines.
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 1, 0, 255, 0, 0);
        pixel(vdp, 1, 16, 0, 0, 255);
        reg(vdp, 0, 0x24); // Left-column mask uses backdrop, even over the gap and sprite.
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 0, 255, 0);
        pixel(vdp, 1, 0, 0, 255, 0);
        pixel(vdp, 8, 0, 255, 0, 0);
    }

    void sprites()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        write(vdp, 0x11, 3, {0x03, 0x0c, 0x30});
        // Banco superior de padrões: sprite 0 vermelho, sprite 1 verde.
        for (unsigned tile = 0; tile < 2; ++tile)
        {
            command(vdp, 0x2000 + tile * 32, 1);
            for (unsigned row = 0; row < 8; ++row)
                for (unsigned plane = 0; plane < 4; ++plane)
                    vdp.writeData(plane == tile ? 0xff : 0);
        }
        write(vdp, 0x3f00, 1, {0xff, 0xff, 0xd0});
        write(vdp, 0x3f80, 1, {10, 0, 14, 1});
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2);
        pixel(vdp, 10, 0, 255, 0, 0);
        pixel(vdp, 14, 0, 255, 0, 0); // O primeiro sprite tem prioridade.
        pixel(vdp, 20, 0, 0, 255, 0);
        check((vdp.readStatus() & 0x20) != 0, "Overlapping opaque sprites must collide");
        check(!(vdp.readStatus() & 0x60), "Reading status must clear collision and overflow");
        vdp.setRenderSprites(false);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 10, 0, 0, 0, 0);
        check((vdp.readStatus() & 0x20) != 0, "Hidden sprites must still collide");
        vdp.setRenderSprites(true);
        // Tile de fundo prioritário bloqueia sprites apenas nos pixels não transparentes.
        write(vdp, 0x3f80, 1, {8, 0});
        write(vdp, 32, 1, {0x80, 0, 0, 0});
        write(vdp, 0x3802, 1, {1, 0x10});
        write(vdp, 1, 3, {0x30});
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 8, 0, 0, 0, 255);
        pixel(vdp, 10, 0, 255, 0, 0);
        vdp.setRenderTiles(false);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 10, 0, 255, 0, 0);

        // Nove sprites com a mesma linha: o nono é visível só com o hack, mas o overflow permanece.
        vdp.setRenderTiles(true);
        command(vdp, 0x3f00, 1);
        for (unsigned i = 0; i < 9; ++i)
            vdp.writeData(0xff);
        vdp.writeData(0xd0);
        command(vdp, 0x3f80, 1);
        for (unsigned i = 0; i < 9; ++i)
        {
            vdp.writeData(static_cast<std::uint8_t>(16 + i * 16));
            vdp.writeData(0);
        }
        vdp.setSpriteUnlimited(false);
        vdp.readStatus();
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 144, 0, 0, 0, 0);
        check((vdp.readStatus() & 0x40) != 0, "Ninth sprite must set overflow with hardware limit");
        vdp.setSpriteUnlimited(true);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 144, 0, 255, 0, 0);
        check((vdp.readStatus() & 0x40) != 0, "Unlimited sprites must preserve overflow");
        // Shift à esquerda, clip horizontal e tabela terminada por D0.
        write(vdp, 0x3f00, 1, {0xff, 0xd0, 0xff});
        write(vdp, 0x3f80, 1, {4, 0, 100, 0, 120, 0});
        reg(vdp, 0, 0x0c);
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 0, 0, 255, 0, 0);
        pixel(vdp, 4, 0, 0, 0, 0);
        pixel(vdp, 112, 0, 0, 0, 0);
        // 8x16 usa índice par, inclusive quando o índice programado é ímpar.
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x42);
        write(vdp, 0x3f80, 1, {16, 1});
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 16, 0, 255, 0, 0);
        pixel(vdp, 16, 8, 0, 255, 0);
    }

    void spriteStatusEdges()
    {
        constexpr unsigned lineTicks = SegaVDP::DotsPerLine * 2;
        const auto setup = [](SegaVDP &vdp, unsigned y, unsigned x, unsigned count)
        {
            normalTables(vdp);
            reg(vdp, 0, 4);
            command(vdp, 0x2000, 1);
            for (unsigned row = 0; row < 8; ++row)
                for (auto value : {0xff, 0, 0, 0})
                    vdp.writeData(static_cast<std::uint8_t>(value));
            command(vdp, 0x3f00, 1);
            for (unsigned i = 0; i < count; ++i)
                vdp.writeData(static_cast<std::uint8_t>(y));
            vdp.writeData(0xd0);
            command(vdp, 0x3f80, 1);
            for (unsigned i = 0; i < count; ++i)
            {
                vdp.writeData(static_cast<std::uint8_t>(x));
                vdp.writeData(0);
            }
        };

        // BLK inibe colisões, mas não a seleção de sprites nem o overflow.
        for (bool unlimited : {false, true})
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            setup(vdp, 0, 16, 9);
            vdp.setSpriteUnlimited(unlimited);
            vdp.runMasterTicks(2 * lineTicks);
            check((vdp.readStatus() & 0x60) == 0x40, "Blanked display must report overflow, but no collision");
            vdp.runMasterTicks(lineTicks);
            check((vdp.readStatus() & 0x60) == 0x40, "Overflow must recur on the next line while blanked");
            vdp.setRenderSprites(false);
            reg(vdp, 1, 0x40);
            vdp.runMasterTicks(lineTicks);
            check((vdp.readStatus() & 0x60) == 0x60, "Display enable restores collisions even when sprites are hidden");
        }

        // A máscara da coluna esquerda também inibe colisões nos pixels 0–7.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            setup(vdp, 0xff, 0, 2);
            reg(vdp, 0, 0x24);
            reg(vdp, 1, 0x40);
            vdp.setRenderSprites(false);
            vdp.runMasterTicks(lineTicks);
            check(!(vdp.readStatus() & 0x20), "Overlaps wholly inside the masked column must not collide");
            write(vdp, 0x3f80, 1, {1, 0, 1, 0});
            vdp.runMasterTicks(8 * 2);
            check(!(vdp.readStatus() & 0x20), "Masked pixels must not set collision");
            vdp.runMasterTicks(10 * 2);
            check(!(vdp.readStatus() & 0x20), "Collision must wait for the sprite processing delay");
            vdp.runMasterTicks(2);
            check(vdp.readStatus() & 0x20, "Collision must be detected after the first unmasked pixel");
            reg(vdp, 0, 4);
            vdp.runMasterTicks(lineTicks - 19 * 2);
            vdp.runMasterTicks(24);
            check(vdp.readStatus() & 0x20, "Unmasking the left column restores collisions there");
        }

        // A colisão do último pixel ativo amadurece durante a borda direita.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            setup(vdp, 0xff, 255, 2);
            reg(vdp, 1, 0x40);
            vdp.runMasterTicks(265 * 2);
            check(!(vdp.readStatus() & 0x20), "Rightmost collision must not latch early");
            auto saved = std::make_unique<SegaVDP::Snapshot>();
            vdp.snapshot(*saved);
            vdp.runMasterTicks(2);
            check(vdp.readStatus() & 0x20, "Rightmost active pixel must collide in the right border");
            vdp.restore(*saved);
            vdp.runMasterTicks(2);
            check(vdp.readStatus() & 0x20, "Restoring before a delayed collision must preserve its timing");
        }

        // Testes posteriores em hardware contestam a colisão apenas fora da área ativa.
        for (unsigned y : {191u, 240u})
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            setup(vdp, y, 16, 2);
            reg(vdp, 1, 0x40);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            check(!(vdp.readStatus() & 0x20), "Sprites wholly outside the active area must not collide");
        }
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            setup(vdp, 190, 16, 2);
            reg(vdp, 1, 0x40);
            vdp.runMasterTicks(SegaVDP::Height * lineTicks);
            check(vdp.readStatus() & 0x20, "Sprites overlapping on the last active line must collide");
        }
    }

    void sms1TableMasks()
    {
        // R2 bit 0 controla o espelhamento das linhas 16–27 do mapa.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            normalTables(vdp);
            reg(vdp, 0, 4);
            reg(vdp, 1, 0x40);
            for (unsigned tile = 1; tile <= 2; ++tile)
            {
                command(vdp, tile * 32, 1);
                for (unsigned row = 0; row < 8; ++row)
                    for (unsigned plane = 0; plane < 4; ++plane)
                        vdp.writeData(plane == tile - 1 ? 0xff : 0);
            }
            write(vdp, 0x3800, 1, {1, 0});
            write(vdp, 0x3c00, 1, {2, 0});
            write(vdp, 1, 3, {0x03, 0x0c});
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 0, 128, 0, 255, 0);
            reg(vdp, 2, 0x0e);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 0, 128, 255, 0, 0);
        }
        // R3 e R4 afetam os dois pares de bitplanes de forma independente.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            normalTables(vdp);
            reg(vdp, 0, 4);
            reg(vdp, 1, 0x40);
            write(vdp, 0x3800, 1, {0, 1});
            write(vdp, 0x2000, 1, {0x80, 0, 0x80, 0});
            write(vdp, 1, 3, {0x03, 0, 0, 0x0c, 0x3f});
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 0, 0, 255, 255, 255);
            reg(vdp, 3, 0x7f);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 0, 0, 0, 255, 0);
            reg(vdp, 3, 0xff);
            reg(vdp, 4, 3);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 0, 0, 255, 0, 0);
        }
        // R5 bit 0 seleciona a metade da SAT usada para X e índice de padrão.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            normalTables(vdp);
            reg(vdp, 0, 4);
            reg(vdp, 1, 0x40);
            vdp.setRenderTiles(false);
            write(vdp, 0x3f00, 1, {16, 0xd0});
            write(vdp, 0x3f80, 1, {32, 1});
            write(vdp, 0x3a00, 1, {0x80, 0, 0, 0});
            write(vdp, 0x2020, 1, {0, 0x80, 0, 0});
            write(vdp, 17, 3, {0x03, 0x0c});
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 32, 17, 0, 255, 0);
            reg(vdp, 5, 0x7e);
            vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(vdp, 16, 17, 255, 0, 0);
            pixel(vdp, 32, 17, 0, 0, 0);
        }
        // Os bits 1 e 0 de R6 mascaram A12 e A11 do padrão de sprite.
        {
            auto instance = std::make_unique<SegaVDP>();
            auto &vdp = *instance;
            normalTables(vdp);
            reg(vdp, 0, 4);
            reg(vdp, 1, 0x40);
            vdp.setRenderTiles(false);
            write(vdp, 0x3f00, 1, {0xff, 0xd0});
            write(vdp, 0x3f80, 1, {16, 0xc0});
            for (unsigned i = 0; i < 4; ++i)
            {
                command(vdp, 0x2000 + i * 0x0800, 1);
                for (unsigned plane = 0; plane < 4; ++plane)
                    vdp.writeData(((i + 1) & (1u << plane)) ? 0x80 : 0);
            }
            write(vdp, 17, 3, {0x03, 0x0c, 0x30, 0x3f});
            for (unsigned i = 0; i < 4; ++i)
            {
                reg(vdp, 6, 4 + i);
                vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
                const unsigned colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
                pixel(vdp, 16, 0, colors[i][0], colors[i][1], colors[i][2]);
            }
        }
    }

    void spriteSelectionPipeline()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        vdp.setRenderTiles(false);
        write(vdp, 0x3f00, 1, {0, 0xd0});
        write(vdp, 0x3f80, 1, {16, 0});
        write(vdp, 0x2000, 1, {0xff, 0, 0, 0});
        write(vdp, 17, 3, {0x03, 0x0c});

        // Linha 0 seleciona o sprite para a linha 1. Mudanças de Y posteriores
        // afetam a linha 2, mas X e padrão ainda são buscados para a linha 1.
        constexpr unsigned selectedTicks = (SegaVDP::Width + 1) * 2;
        vdp.runMasterTicks(selectedTicks);
        write(vdp, 0x3f00, 1, {0xd0});
        write(vdp, 0x3f80, 1, {32, 0});
        write(vdp, 0x2000, 1, {0, 0xff, 0, 0});
        vdp.runMasterTicks(SegaVDP::Height * SegaVDP::DotsPerLine * 2 - selectedTicks);
        pixel(vdp, 16, 1, 0, 0, 0);
        pixel(vdp, 32, 1, 0, 255, 0);
        pixel(vdp, 32, 2, 0, 0, 0);

        // A última linha do quadro prepara a primeira linha ativa do seguinte.
        write(vdp, 0x3f00, 1, {0xff, 0xd0});
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        pixel(vdp, 32, 0, 0, 255, 0);
    }

    void spriteOverflowTiming()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        normalTables(vdp);
        reg(vdp, 0, 4);
        reg(vdp, 1, 0x40);
        command(vdp, 0x3f00, 1);
        for (unsigned i = 0; i < 9; ++i)
            vdp.writeData(0);
        vdp.writeData(0xd0);
        // A leitura antes do evento não deve observar overflow; a seguinte deve.
        vdp.runMasterTicks(317 * 2);
        check(!(vdp.readStatus() & 0x40), "Overflow must not be signalled while the next line is still being prepared");
        vdp.runMasterTicks(2);
        check(vdp.readStatus() & 0x40, "Overflow is signalled in HBlank for the selected next line");
        vdp.runMasterTicks(2);
        check(!(vdp.readStatus() & 0x40), "Reading status clears overflow until the next event");
    }

    void lineInterrupts()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        constexpr unsigned lineTicks = SegaVDP::DotsPerLine * 2;
        constexpr unsigned hblankTicks = 319 * 2 + 2;
        reg(vdp, 10, 3);
        // O contador é carregado durante VBlank, mesmo com a interrupção desabilitada.
        vdp.runMasterTicks(SegaVDP::MasterTicksPerFrame);
        vdp.readStatus();
        check(vdp.state().lineCounter == 2 && !vdp.interrupt(), "The first active-line count occurs during the preceding HBlank");
        vdp.runMasterTicks(2 * lineTicks + hblankTicks - 1);
        check(!vdp.state().lineInterruptPending && vdp.state().lineCounter == 0, "Line IRQ must not trigger early");
        vdp.runMasterTicks(1);
        check(vdp.state().lineInterruptPending && vdp.state().lineCounter == 3 && !vdp.interrupt(),
              "Line counter underflow latches while disabled and reloads R10");
        reg(vdp, 0, 0x10);
        check(vdp.interrupt(), "Enabling a pending line interrupt must assert IRQ");
        reg(vdp, 0, 0);
        check(!vdp.interrupt() && vdp.state().lineInterruptPending, "Disabling IRQ preserves pending line interrupt");
        reg(vdp, 0, 0x10);
        check(!(vdp.readStatus() & 0x80) && !vdp.interrupt(), "Status acknowledges line interrupt without VBlank flag");
        // Uma escrita após a IRQ aguarda a próxima captura de R8.
        reg(vdp, 8, 8);
        vdp.runMasterTicks(lineTicks);
        check(vdp.state().horizontalScroll == 8, "A line IRQ handler must update scroll for the next scanline");
        reg(vdp, 10, 1);
        vdp.runMasterTicks(2 * lineTicks);
        check(!vdp.state().lineInterruptPending && vdp.state().lineCounter == 0, "R10 write must not immediately change counter");
        vdp.runMasterTicks(lineTicks);
        check(vdp.state().lineInterruptPending && vdp.state().lineCounter == 1, "Underflow reloads the new R10 value");
        vdp.readStatus();
        vdp.runMasterTicks(lineTicks);
        check(!vdp.interrupt(), "Reload value one waits for two scanlines");
        vdp.runMasterTicks(lineTicks);
        check(vdp.interrupt(), "Reloaded line interrupt must recur");
        vdp.runMasterTicks((SegaVDP::Height - 8) * lineTicks + 4);
        check((vdp.state().status & 0x80) && vdp.state().lineInterruptPending, "Frame and line interrupts can both be pending");
        check((vdp.readStatus() & 0x80) && !vdp.interrupt(), "Status read acknowledges both interrupt sources");
    }

    void horizontalTiming()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        reg(vdp, 0, 0x10);
        reg(vdp, 10, 0);
        vdp.runMasterTicks(316 * 2);
        reg(vdp, 8, 8);
        check(!vdp.interrupt() && vdp.verticalCounter() == 0, "HBlank events must not happen early");
        vdp.runMasterTicks(2);
        check(vdp.verticalCounter() == 1 && !vdp.interrupt(), "V counter advances before the line interrupt");
        vdp.runMasterTicks(4);
        check(vdp.state().horizontalScroll == 8 && !vdp.interrupt(), "R8 must be latched before HINT");
        reg(vdp, 8, 16);
        vdp.runMasterTicks(2);
        check(vdp.interrupt() && vdp.state().horizontalScroll == 8 && vdp.verticalCounter() == 1,
              "HINT must occur after R8 latch");
        vdp.readStatus();
        vdp.runMasterTicks(SegaVDP::DotsPerLine * 2);
        check(vdp.state().horizontalScroll == 16, "R8 writes after the latch apply on the following line");
        vdp.runMasterTicks(260 * SegaVDP::DotsPerLine * 2 - 8);
        check(vdp.verticalCounter() == 0xff, "Final V counter value before frame wrap");
        vdp.runMasterTicks(2);
        check(vdp.verticalCounter() == 0, "V counter wraps during the last horizontal blank");
    }

    void horizontalCounterSequence()
    {
        SegaVDP vdp;
        // The counter advances every two dots and skips 94–E8 during retrace.
        vdp.runMasterTicks(2 * 293);
        check(vdp.horizontalCounter() == 0x93, "H counter before retrace");
        vdp.runMasterTicks(4);
        check(vdp.horizontalCounter() == 0xe9, "H counter jumps from 93 to E9");
        vdp.runMasterTicks(2 * 44);
        check(vdp.horizontalCounter() == 0xff, "H counter reaches FF before wrap");
        vdp.runMasterTicks(4);
        check(vdp.horizontalCounter() == 0, "H counter wraps to zero");
    }

    void verticalCounter()
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        for (unsigned line = 0; line < SegaVDP::LinesPerFrame; ++line)
        {
            const unsigned expected = line <= 0xda ? line : line - 6;
            check(vdp.verticalCounter() == expected, "NTSC vertical counter sequence");
            vdp.runMasterTicks(SegaVDP::DotsPerLine * 2);
        }
        check(vdp.verticalCounter() == 0, "Vertical counter returns to zero at next frame");
    }
}

int main()
{
    try
    {
        auto instance = std::make_unique<SegaVDP>();
        auto &vdp = *instance;
        command(vdp, 0x3fff, 1);
        vdp.writeData(0xa5);
        vdp.writeData(0x5a);
        check(vdp.state().vram[0x3fff] == 0xa5 && vdp.state().vram[0] == 0x5a && vdp.state().address == 1,
              "VRAM writes and address wrap");
        command(vdp, 0x3fff, 0);
        check(vdp.state().address == 0 && vdp.readData() == 0xa5 && vdp.readData() == 0x5a,
              "VRAM prefetch and buffered reads");
        command(vdp, 0x12ff, 1);
        vdp.writeControl(0x34);
        vdp.writeData(0x77);
        check(vdp.state().vram[0x1234] == 0x77 && !vdp.state().secondControlByte,
              "First control byte changes address; data cancels latch");
        vdp.writeControl(0x88);
        vdp.readStatus();
        command(vdp, 0x0004, 2);
        // Register commands use the low byte as data and bits 11..8 as the index.
        vdp.writeControl(0x6b);
        vdp.writeControl(0x8a);
        check(vdp.state().registers[10] == 0x6b && !vdp.state().secondControlByte, "Register write and latch reset");
        command(vdp, 0x003f, 3);
        vdp.writeData(0xff);
        vdp.writeData(0x12);
        check(vdp.state().cram[31] == 0x3f && vdp.state().cram[0] == 0x12 && vdp.state().address == 0x41,
              "SMS 6-bit CRAM and 32-byte mirror");
        command(vdp, 0x003f, 3);
        vdp.readData();
        check(vdp.state().readBuffer == vdp.state().vram[0x003f], "CRAM mode data reads use VRAM");
        {
            auto gg = std::make_unique<SegaVDP>(SegaVDP::Model::GameGear);
            // Uma escrita par apenas prepara o latch, sem modificar a cor anterior.
            write(*gg, 0x20, 3, {0x21, 0x03});
            write(*gg, 0x20, 3, {0xfe});
            gg->runMasterTicks(SegaVDP::MasterTicksPerFrame);
            const auto first = gg->frame();
            check(first.width == 160 && first.height == 144 && first.pitch == 256 * 4,
                  "Game Gear must expose the central 160x144 image with the original pitch");
            pixel(*gg, 0, 0, 17, 34, 51);
            // A escrita �mpar usa o latch anterior mesmo em outro endere�o ou espelho.
            write(*gg, 0x61, 3, {0x04});
            gg->runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(*gg, 0, 0, 238, 255, 68);
            pixel(*gg, 159, 143, 238, 255, 68);
        }
        {
            auto gg = std::make_unique<SegaVDP>(SegaVDP::Model::GameGear);
            write(*gg, 0x20, 3, {0x00, 0x0f, 0x0f, 0x00});
            gg->runMasterTicks((24 * SegaVDP::DotsPerLine + 48) * 2);
            reg(*gg, 7, 1);
            gg->runMasterTicks(2);
            reg(*gg, 7, 0);
            gg->runMasterTicks(SegaVDP::MasterTicksPerFrame);
            pixel(*gg, 0, 0, 255, 0, 0);
            pixel(*gg, 1, 0, 0, 0, 255);
        }
        rasterAndBackground();
        scrolling();
        fineScrollEdge();
        sprites();
        spriteStatusEdges();
        sms1TableMasks();
        spriteSelectionPipeline();
        spriteOverflowTiming();
        lineInterrupts();
        horizontalTiming();
        horizontalCounterSequence();
        verticalCounter();
        {
            auto pal = std::make_unique<SegaVDP>(true);
            const unsigned lineTicks = SegaVDP::DotsPerLine * 2;
            pal->runMasterTicks(242 * lineTicks);
            check(pal->verticalCounter() == 0xf2, "PAL counter before wrap");
            pal->runMasterTicks(lineTicks);
            check(pal->verticalCounter() == 0xba, "PAL counter wrap at F2");
            pal->runMasterTicks(70 * lineTicks);
            check(pal->state().line == 0 && pal->verticalCounter() == 0, "PAL frame has 313 lines");
        }
        std::cout << "Sega VDP ports and memory passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
