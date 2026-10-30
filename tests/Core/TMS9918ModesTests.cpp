#include "Core/VDP/TMS9918/TMS9918.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;
    using Byte = std::uint8_t;
    constexpr Byte RGB[16][3] = {{0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120}, {84, 85, 237}, {125, 118, 252}, {212, 82, 77}, {66, 235, 245}, {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128}, {33, 176, 59}, {201, 91, 186}, {204, 204, 204}, {255, 255, 255}};

    // Verifica uma propriedade e identifica a operação que falhou.
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Configura registradores pelas portas, preservando os mesmos efeitos vistos pela CPU.
    void reg(VDP &vdp, unsigned index, unsigned value)
    {
        vdp.writeControl(static_cast<Byte>(value));
        vdp.writeControl(static_cast<Byte>(0x80 | index));
    }

    // Escreve um bloco na VRAM pela porta de dados, com incremento automático.
    template <class Data>
    void upload(VDP &vdp, unsigned address, const Data &bytes)
    {
        vdp.writeControl(static_cast<Byte>(address));
        vdp.writeControl(static_cast<Byte>(0x40 | (address >> 8)));
        for (auto value : bytes)
            vdp.writeData(value);
    }

    // Confere a cor de um pixel RGBA usando a paleta de apresentação adotada pelo projeto.
    void pixel(const R2SEGA8::Core::VideoFrame &frame, unsigned x, unsigned y, unsigned color)
    {
        const auto *rgba = frame.rgba + y * frame.pitch + x * 4;
        check(rgba[0] == RGB[color][0] && rgba[1] == RGB[color][1] && rgba[2] == RGB[color][2] && rgba[3] == 255,
              "Background pixel mismatch");
    }

    // Testa os três bancos, cores por linha e as máscaras independentes das tabelas de Graphics II.
    void graphicsII()
    {
        auto vdp = std::make_unique<VDP>();
        // As cenas isolam o background encerrando explicitamente a tabela de sprites.
        reg(*vdp, 5, 0x7f);
        upload(*vdp, 0x3f80, std::array<Byte, 1>{0xd0});
        reg(*vdp, 0, 2);
        reg(*vdp, 1, 0xe0);
        reg(*vdp, 2, 6);
        reg(*vdp, 3, 0xff);
        reg(*vdp, 4, 3);
        reg(*vdp, 7, 4);
        std::array<Byte, 768> names{};
        for (unsigned i = 0; i < names.size(); ++i)
            names[i] = static_cast<Byte>(i);
        upload(*vdp, 0x1800, names);
        std::array<Byte, 0x1800> patterns{}, colors{};
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned name = 0; name < 256; ++name)
                for (unsigned row = 0; row < 8; ++row)
                {
                    const unsigned i = bank * 0x800 + name * 8 + row;
                    patterns[i] = static_cast<Byte>((name * 13) ^ (row * 29) ^ (bank * 0x55));
                    colors[i] = static_cast<Byte>(((bank * 4 + row) % 16 << 4) | ((name + row) % 16));
                }
        upload(*vdp, 0, patterns);
        upload(*vdp, 0x2000, colors);
        // A tabela abaixo descreve quais bancos físicos sobrevivem às máscaras de R4/R3.
        constexpr unsigned banks[4][3] = {{0, 0, 0}, {0, 1, 0}, {0, 0, 2}, {0, 1, 2}};
        for (unsigned patternMask = 0; patternMask < 4; ++patternMask)
            for (unsigned colorMask = 0; colorMask < 4; ++colorMask)
            {
                reg(*vdp, 4, patternMask);
                reg(*vdp, 3, 0x9f | (colorMask << 5));
                vdp->runMasterTicks(VDP::MasterTicksPerFrame);
                const auto frame = vdp->frame();
                check(vdp->interrupt(), "Graphics II keeps NTSC interrupt timing");
                vdp->readStatus();
                for (unsigned y = 0; y < 192; ++y)
                    for (unsigned x = 0; x < 256; ++x)
                    {
                        const unsigned bank = y / 64, name = ((y / 8) * 32 + x / 8) % 256, row = y % 8;
                        const Byte bits = patterns[banks[patternMask][bank] * 0x800 + name * 8 + row];
                        const Byte pair = colors[banks[colorMask][bank] * 0x800 + name * 8 + row];
                        unsigned color = (bits & (0x80 >> (x % 8))) ? pair >> 4 : pair & 15;
                        pixel(frame, x, y, color ? color : 4);
                    }
            }
        // Inverte as bases físicas para detectar implementações que fixam padrões em 0000 e cores em 2000.
        upload(*vdp, 0, colors);
        upload(*vdp, 0x2000, patterns);
        reg(*vdp, 3, 0x7f);
        reg(*vdp, 4, 7);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const unsigned i = (y / 64) * 0x800 + (((y / 8) * 32 + x / 8) % 256) * 8 + y % 8;
                const unsigned color = (patterns[i] & (0x80 >> (x % 8))) ? colors[i] >> 4 : colors[i] & 15;
                pixel(vdp->frame(), x, y, color ? color : 4);
            }
    }

    // Confere o acoplamento não documentado de R3 aos nomes de padrão nos chips TMS99xxA.
    void graphicsIIMaskedNames()
    {
        auto vdp = std::make_unique<VDP>();
        // As cenas isolam o background encerrando explicitamente a tabela de sprites.
        reg(*vdp, 5, 0x7f);
        upload(*vdp, 0x3f80, std::array<Byte, 1>{0xd0});
        reg(*vdp, 0, 2);
        reg(*vdp, 1, 0xc0);
        reg(*vdp, 2, 6);
        reg(*vdp, 3, 0x80);
        reg(*vdp, 4, 3);
        std::array<Byte, 768> names{};
        names.fill(8);
        upload(*vdp, 0x1800, names);
        std::array<Byte, 0x1800> patterns{};
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned row = 0; row < 8; ++row)
            {
                patterns[bank * 0x800 + row] = static_cast<Byte>(bank == 0 ? 0x80 >> row : bank == 1 ? ~(0x80 >> row)
                                                                                                     : 0xaa);
                patterns[bank * 0x800 + 64 + row] = 0xff;
            }
        upload(*vdp, 0, patterns);
        std::array<Byte, 72> colors{};
        colors.fill(0x61);
        for (unsigned row = 0; row < 8; ++row)
            colors[row] = 0xf2;
        upload(*vdp, 0x2000, colors);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const unsigned row = y % 8, bank = y / 64;
                const bool bit = bank == 0 ? x % 8 == row : bank == 1 ? x % 8 != row
                                                                      : x % 2 == 0;
                pixel(vdp->frame(), x, y, bit ? 15 : 2);
            }
    }

    // Exercita 960 nomes, os seis bits superiores do padrão, R7 e as margens de 6/10 pixels.
    void text()
    {
        auto vdp = std::make_unique<VDP>();
        // As cenas isolam o background encerrando explicitamente a tabela de sprites.
        reg(*vdp, 5, 0x7f);
        upload(*vdp, 0x3f80, std::array<Byte, 1>{0xd0});
        reg(*vdp, 0, 0);
        reg(*vdp, 1, 0xf0);
        reg(*vdp, 2, 15);
        reg(*vdp, 3, 0xff);
        reg(*vdp, 4, 3);
        reg(*vdp, 7, 0xf4);
        std::array<Byte, 960> names{};
        std::array<Byte, 2048> patterns{};
        for (unsigned i = 0; i < names.size(); ++i)
            names[i] = static_cast<Byte>((i / 40) * 11 + (i % 40) * 7);
        for (unsigned name = 0; name < 256; ++name)
            for (unsigned row = 0; row < 8; ++row)
                patterns[name * 8 + row] = static_cast<Byte>(((name * 37) ^ (row * 19)) | 3);
        upload(*vdp, 0x3c00, names);
        upload(*vdp, 0x1800, patterns);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                unsigned color = 4;
                if (x >= 6 && x < 246)
                {
                    const unsigned column = (x - 6) / 6;
                    const unsigned name = ((y / 8) * 11 + column * 7) % 256;
                    const bool foreground = (((name * 37) ^ ((y % 8) * 19)) & (0x80 >> ((x - 6) % 6))) != 0;
                    if (foreground)
                        color = 15;
                }
                pixel(vdp->frame(), x, y, color);
            }
        reg(*vdp, 7, 4);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
                pixel(vdp->frame(), x, y, 4);
        reg(*vdp, 7, 0xf0);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        pixel(vdp->frame(), 0, 0, 0);
        pixel(vdp->frame(), 255, 191, 0);
        check(vdp->interrupt(), "Text preserves VBlank/INT independently of background colors");
    }

    // Confere as quatro áreas de cada nome e os oito bytes alternados a cada grupo de 32 linhas.
    void multicolor()
    {
        auto vdp = std::make_unique<VDP>();
        // As cenas isolam o background encerrando explicitamente a tabela de sprites.
        reg(*vdp, 5, 0x7f);
        upload(*vdp, 0x3f80, std::array<Byte, 1>{0xd0});
        reg(*vdp, 1, 0xe8);
        reg(*vdp, 2, 6);
        reg(*vdp, 3, 0xff);
        reg(*vdp, 4, 1);
        reg(*vdp, 7, 6);
        std::array<Byte, 768> names{};
        std::array<Byte, 2048> patterns{};
        for (unsigned y = 0; y < 24; ++y)
            for (unsigned x = 0; x < 32; ++x)
                names[y * 32 + x] = static_cast<Byte>(y * 19 + x * 17);
        for (unsigned name = 0; name < 256; ++name)
            for (unsigned row = 0; row < 8; ++row)
                patterns[name * 8 + row] = static_cast<Byte>(((name + row) % 16 << 4) | ((name * 3 + row * 5 + 1) % 16));
        upload(*vdp, 0x1800, names);
        upload(*vdp, 0x0800, patterns);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        for (unsigned y = 0; y < 192; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const unsigned name = ((y / 8) * 19 + (x / 8) * 17) % 256;
                const unsigned row = ((y / 8) % 4) * 2 + (y % 8 >= 4 ? 1 : 0);
                unsigned color = x % 8 < 4 ? (name + row) % 16 : (name * 3 + row * 5 + 1) % 16;
                pixel(vdp->frame(), x, y, color ? color : 6);
            }
        check(vdp->interrupt(), "Multicolor preserves VBlank/INT");
        // Retoma uma célula depois de somente parte de seus pixels ter sido desenhada.
        vdp->runMasterTicks((VDP::LinesPerFrame - 192) * VDP::MasterTicksPerLine + 11);
        const auto saved = std::make_unique<VDP::State>(vdp->state());
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        const auto expected = std::make_unique<VDP::State>(vdp->state());
        vdp->restore(*saved);
        for (unsigned i = 0; i < VDP::MasterTicksPerFrame; ++i)
            vdp->tick();
        check(vdp->state().images == expected->images && vdp->state().dot == expected->dot && vdp->state().line == expected->line,
              "Mid-cell snapshot did not preserve the multicolor latch and pixel phase");
    }
} // namespace

int main()
{
    try
    {
        graphicsII();
        graphicsIIMaskedNames();
        text();
        multicolor();
        std::cout << "Graphics II banks/masks, Text, Multicolor, pixel colors, timing and mid-cell restore passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}