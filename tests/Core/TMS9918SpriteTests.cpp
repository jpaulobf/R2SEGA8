#include "Core/VDP/TMS9918/TMS9918.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;
    using Byte = std::uint8_t;
    constexpr unsigned SAT = 0x3f80, Patterns = 0x2800;

    // Identifica a propriedade do VDP que divergiu da cena esperada.
    void check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Programa os registradores pelas portas reais de controle.
    void reg(VDP &vdp, unsigned index, unsigned value)
    {
        vdp.writeControl(static_cast<Byte>(value));
        vdp.writeControl(static_cast<Byte>(0x80 | index));
    }

    // Prepara os bytes da cena por snapshot, isolando a renderização do protocolo de portas.
    void upload(VDP &vdp, unsigned address, std::initializer_list<Byte> data)
    {
        auto state = std::make_unique<VDP::State>(vdp.state());
        for (auto byte : data)
            state->vram[(address++) & 0x3fff] = byte;
        vdp.restore(*state);
    }

    // Cria uma cena de hardware com limite explícito, independente da preferência padrão do emulador.
    std::unique_ptr<VDP> scene(unsigned options = 0)
    {
        auto vdp = std::make_unique<VDP>();
        vdp->setSpriteUnlimited(false);
        reg(*vdp, 1, 0xc0 | options);
        reg(*vdp, 2, 6);
        reg(*vdp, 3, 0x80);
        reg(*vdp, 5, 0xff);
        reg(*vdp, 6, 0xfd);
        reg(*vdp, 7, 4);
        upload(*vdp, SAT, {0xd0});
        return vdp;
    }

    // Escreve uma entrada de sprite; a posição vertical continua no formato Y do hardware.
    void sprite(VDP &vdp, unsigned index, Byte y, Byte x, Byte name, Byte color)
    {
        upload(vdp, SAT + index * 4, {y, x, name, color});
    }

    // Preenche as oito linhas de um padrão sólido para cenas de prioridade e limites.
    void solid(VDP &vdp, unsigned name = 0)
    {
        upload(vdp, Patterns + name * 8, {255, 255, 255, 255, 255, 255, 255, 255});
    }

    // Confere a paleta das cores usadas nas cenas sem depender do compositor de sprites.
    void pixel(const VDP &vdp, unsigned x, unsigned y, unsigned color)
    {
        constexpr Byte colors[4][3] = {{84, 85, 237}, {255, 255, 255}, {33, 200, 66}, {212, 82, 77}};
        const auto frame = vdp.frame();
        const auto *p = frame.rgba + y * frame.pitch + x * 4;
        check(p[0] == colors[color][0] && p[1] == colors[color][1] && p[2] == colors[color][2] && p[3] == 255,
              "Sprite pixel geometry, clipping or priority mismatch");
    }

    // Aquece a seleção da linha zero através do pré-raster e reconhece o VBlank anterior.
    void warm(VDP &vdp)
    {
        vdp.runMasterTicks(VDP::MasterTicksPerFrame);
        vdp.readStatus();
    }

    // Confere todos os pixels nas quatro combinações de SIZE/MAG e nas bordas da tela.
    void geometry()
    {
        for (unsigned options = 0; options < 4; ++options)
            for (unsigned placement = 0; placement < 6; ++placement)
            {
                auto vdp = scene(options);
                // Quatro quadrantes distintos: diagonal, diagonal inversa, sólido e alternado.
                upload(*vdp, Patterns + 0xfc * 8, {128, 64, 32, 16, 8, 4, 2, 1, 1, 2, 4, 8, 16, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 170, 85, 170, 85, 170, 85, 170, 85});
                const unsigned size = options & 2 ? 16 : 8, scale = options & 1 ? 2 : 1;
                constexpr Byte ys[] = {39, 0xff, 0xfa, 190, 0xe0, 80};
                constexpr Byte xs[] = {60, 0, 28, 252, 40, 0};
                const bool early = placement == 2 || placement == 5;
                // Em SIZE=1, FF deve selecionar FC; em SIZE=0, usa-se FC explicitamente.
                sprite(*vdp, 0, ys[placement], xs[placement], size == 16 ? 0xff : 0xfc, early ? 0x8f : 15);
                upload(*vdp, SAT + 4, {0xd0});
                warm(*vdp);
                vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine);
                const int left = int(xs[placement]) - (early ? 32 : 0);
                const int top = placement == 0   ? 40
                                : placement == 1 ? 0
                                : placement == 2 ? -5
                                : placement == 3 ? 191
                                : placement == 4 ? 225
                                                 : 81;
                for (unsigned y = 0; y < 192; ++y)
                    for (unsigned x = 0; x < 256; ++x)
                    {
                        const int dx = int(x) - left, dy = int(y) - top;
                        bool bit = false;
                        if (dx >= 0 && dy >= 0 && dx < int(size * scale) && dy < int(size * scale))
                        {
                            const unsigned u = unsigned(dx) / scale, v = unsigned(dy) / scale;
                            bit = u < 8 ? (v < 8 ? u == v : u == 15 - v) : (v < 8 ? true : (u + v) % 2 == 0);
                        }
                        pixel(*vdp, x, y, bit ? 1 : 0);
                    }
                check(!(vdp->state().status & 0x60), "Single sprite cannot collide or overflow");
            }
    }

    // Verifica prioridade sobre tiles, transparência, flags retidas e colisão na fase do pixel.
    void collisions()
    {
        auto vdp = scene();
        solid(*vdp);
        sprite(*vdp, 0, 0xff, 10, 0, 0); // Invisível, mas participa da colisão.
        sprite(*vdp, 1, 0xff, 12, 0, 2);
        sprite(*vdp, 2, 0xff, 14, 0, 6);
        upload(*vdp, SAT + 12, {0xd0});
        warm(*vdp);
        vdp->runMasterTicks(24);
        check(!(vdp->state().status & 0x20), "Collision cannot precede the overlapping pixel");
        vdp->tick();
        check(!(vdp->state().status & 0x20), "Collision respects the master clock divider");
        vdp->tick();
        check((vdp->readStatus() & 0x20) && !(vdp->state().status & 0x20) && !vdp->interrupt(),
              "Transparent pattern collision is acknowledged without generating INT");
        vdp->runMasterTicks(2);
        check(vdp->state().status & 0x20, "A later overlapping pixel can set collision again");
        vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine - 28);
        for (unsigned x = 0; x < 32; ++x)
            pixel(*vdp, x, 0, x >= 12 && x < 20 ? 2 : x >= 20 && x < 22 ? 3
                                                                        : 0);
        check((vdp->readStatus() & 0xa0) == 0xa0 && !(vdp->readStatus() & 0xe0), "All upper status flags clear on read");

        // Padrões vazios não colidem, mesmo com retângulos e cores sobrepostos.
        vdp = scene();
        upload(*vdp, Patterns, {0x80});
        upload(*vdp, Patterns + 8, {0x40});
        sprite(*vdp, 0, 0xff, 10, 0, 15);
        sprite(*vdp, 1, 0xff, 10, 1, 2);
        upload(*vdp, SAT + 8, {0xd0});
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        check(!(vdp->state().status & 0x20), "Only set pattern bits collide");
    }

    // Exercita coincidências horizontais na borda descritas pelo manual da TI.
    void offscreenCollisions()
    {
        for (unsigned options = 0; options < 4; ++options)
            for (bool unlimited : {false, true})
            {
                auto vdp = scene(options);
                vdp->setSpriteUnlimited(unlimited);
                solid(*vdp);
                sprite(*vdp, 0, 39, 0, 0, 0x8f); // X=-32; nenhum pixel chega à imagem.
                sprite(*vdp, 1, 39, 4, 0, 0x80); // X=-28 e cor transparente também colide.
                upload(*vdp, SAT + 8, {0xd0});
                warm(*vdp);
                vdp->runMasterTicks(39ULL * VDP::MasterTicksPerLine + 314 * 2);
                check(!(vdp->state().status & 0x20), "Left-border collision must not occur before the overlap");
                vdp->tick();
                check(!(vdp->state().status & 0x20), "Left-border collision must wait for the second master tick");
                const auto halfDot = std::make_unique<VDP::State>(vdp->state());
                vdp->tick();
                check(vdp->readStatus() & 0x20, "Completely offscreen left sprites must participate in collision");
                vdp->restore(*halfDot);
                vdp->tick();
                check(vdp->readStatus() & 0x20, "Restoring between master ticks must preserve the offscreen collision phase");
                vdp->runMasterTicks(2);
                check(vdp->state().status & 0x20, "Another offscreen overlap must relatch status after a read");

                vdp = scene(options);
                vdp->setSpriteUnlimited(unlimited);
                upload(*vdp, Patterns, {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f});
                sprite(*vdp, 0, 39, 255, 0, 15);
                sprite(*vdp, 1, 39, 255, 0, 2);
                upload(*vdp, SAT + 8, {0xd0});
                warm(*vdp);
                const unsigned firstRight = options & 1 ? 257 : 256;
                vdp->runMasterTicks(40ULL * VDP::MasterTicksPerLine + firstRight * 2);
                check(!(vdp->state().status & 0x20), "Clear visible pattern bit must not collide at X=255");
                vdp->tick();
                check(!(vdp->state().status & 0x20), "Right-border collision must wait for the second master tick");
                vdp->tick();
                check(vdp->state().status & 0x20, "First overlap beyond X=255 must latch collision");
            }
        for (bool unlimited : {false, true})
        {
            auto vdp = scene();
            vdp->setSpriteUnlimited(unlimited);
            solid(*vdp);
            sprite(*vdp, 0, 39, 0, 0, 0x8f);
            for (unsigned i = 1; i < 4; ++i)
                sprite(*vdp, i, 39, static_cast<Byte>(40 * i), 0, 15);
            sprite(*vdp, 4, 39, 4, 0, 0x82);
            upload(*vdp, SAT + 20, {0xd0});
            warm(*vdp);
            vdp->runMasterTicks(39ULL * VDP::MasterTicksPerLine + 315 * 2);
            check(bool(vdp->state().status & 0x20) == unlimited,
                  "An offscreen fifth sprite collides only when the emulator's unlimited option selects it");
            check(vdp->state().status & 0x40, "The fifth sprite must still report overflow in both profiles");
        }
        auto vdp = scene();
        upload(*vdp, Patterns, {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80});
        upload(*vdp, Patterns + 8, {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40});
        sprite(*vdp, 0, 39, 0, 0, 0x8f);
        sprite(*vdp, 1, 39, 0, 1, 0x82);
        upload(*vdp, SAT + 8, {0xd0});
        warm(*vdp);
        vdp->runMasterTicks(40ULL * VDP::MasterTicksPerLine);
        check(!(vdp->state().status & 0x20), "Offscreen rectangles must not collide when their pattern bits do not overlap");
    }

    // Testa o quinto sprite, índice real na SAT, descarte, retenção e bloqueio por VBlank pendente.
    void overflow()
    {
        auto vdp = scene();
        solid(*vdp);
        sprite(*vdp, 0, 100, 0, 0, 15);    // Não selecionado na primeira linha.
        sprite(*vdp, 1, 0xff, 0, 0, 0);    // Transparente conta.
        sprite(*vdp, 2, 0xff, 0, 0, 0x8f); // Fora da tela conta.
        sprite(*vdp, 3, 0xff, 40, 1, 15);  // Padrão vazio conta.
        sprite(*vdp, 4, 0xff, 60, 0, 2);
        sprite(*vdp, 5, 0xff, 100, 0, 6); // Quinto sprite da linha, índice 5 da SAT.
        sprite(*vdp, 6, 0xff, 60, 0, 6);  // Descartado, não causa colisão.
        upload(*vdp, SAT + 28, {0xd0});
        warm(*vdp);
        vdp->runMasterTicks(653);
        check(!(vdp->state().status & 0x40), "Overflow cannot precede the fifth Y evaluation dot");
        vdp->tick();
        check((vdp->state().status & 0x5f) == 0x45 && !vdp->interrupt(), "Fifth sprite index and no sprite INT");
        vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine - 654);
        check((vdp->state().status & 0x7f) == 0x45, "First overflow remains latched and rejected sprites cannot collide");
        pixel(*vdp, 60, 0, 2);
        pixel(*vdp, 100, 0, 0);
        vdp->readStatus();
        // Uma nova linha após o reconhecimento volta a avaliar a SAT.
        vdp->runMasterTicks((VDP::LinesPerFrame - VDP::Height) * VDP::MasterTicksPerLine + 654);
        check((vdp->state().status & 0x5f) == 0x45, "Overflow can latch again after status read");
        upload(*vdp, SAT, {0xd0});
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        check((vdp->state().status & 0x5f) == 0x45, "SAT changes do not clear a latched fifth sprite");
        vdp->readStatus();
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        check(!(vdp->state().status & 0x60), "D0 terminates the SAT before subsequent sprites");

        vdp = scene();
        solid(*vdp);
        for (unsigned i = 0; i < 32; ++i)
            sprite(*vdp, i, i < 27 ? 100 : 0xff, static_cast<Byte>((i - 27) * 16), 0, 15);
        // Última entrada da SAT pode ser o quinto sprite, sem ler além dos 16 KiB.
        warm(*vdp);
        vdp->runMasterTicks(482);
        check((vdp->readStatus() & 0x5f) == 0x5f, "Sprite 31 overflow at the last SAT entry");
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        check(vdp->state().status & 0x80, "Frame flag remains pending");
        vdp->readStatus();
        vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine);
        // Retém F mas limpa apenas 5S via snapshot para isolar a condição documentada.
        auto state = std::make_unique<VDP::State>(vdp->state());
        state->status = 0x80;
        state->line = state->dot = 0;
        state->masterPhase = 0;
        vdp->restore(*state);
        vdp->runMasterTicks(482);
        check(!(vdp->state().status & 0x40), "Pending frame flag inhibits a new overflow flag");
    }

    // Confere os modos com sprites, apagamento, ausência no Text e composição acima de um tile opaco.
    void modes()
    {
        for (unsigned mode : {0u, 2u, 4u, 1u, 8u})
        {
            auto vdp = scene();
            reg(*vdp, 0, mode == 2 ? 2 : 0);
            reg(*vdp, 1, mode == 1 ? 0xd0 : mode == 4 ? 0xc8
                                        : mode == 8   ? 0x80
                                                      : 0xc0);
            upload(*vdp, 0, {255, 255, 255, 255, 255, 255, 255, 255});
            upload(*vdp, 0x2000, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
            solid(*vdp);
            for (unsigned i = 0; i < 5; ++i)
                sprite(*vdp, i, 0xff, 10, 0, 2);
            upload(*vdp, SAT + 20, {0xd0});
            warm(*vdp);
            vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine);
            const bool enabled = mode != 1 && mode != 8;
            pixel(*vdp, 10, 0, enabled ? 2 : 0);
            check(bool(vdp->state().status & 0x60) == enabled, "Text/blanking must inhibit sprite flags");
        }
    }

    // Restaura o raster no meio de uma sobreposição e mantém os pixels já buscados para a linha.
    void snapshot()
    {
        auto vdp = scene(3);
        solid(*vdp);
        sprite(*vdp, 0, 0xff, 20, 0, 15);
        sprite(*vdp, 1, 0xff, 22, 0, 2);
        upload(*vdp, SAT + 8, {0xd0});
        warm(*vdp);
        vdp->runMasterTicks(43);
        // O terminador não apaga os atributos/padrões já buscados para a linha atual.
        upload(*vdp, SAT, {0xd0});
        const auto saved = std::make_unique<VDP::State>(vdp->state());
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        const auto expected = std::make_unique<VDP::State>(vdp->state());
        vdp->restore(*saved);
        vdp->runMasterTicks(VDP::MasterTicksPerFrame);
        check(vdp->state().images == expected->images && vdp->state().status == expected->status &&
                  vdp->state().selectedSprites == expected->selectedSprites && vdp->state().extraFetchPhase == expected->extraFetchPhase &&
                  vdp->state().masterTicks == expected->masterTicks,
              "Snapshot must preserve selected sprite pixels and collision phase");
        saved->sprites[0].index = 32;
        bool rejected = false;
        try
        {
            vdp->restore(*saved);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid restored SAT sprite index rejected");
        vdp->reset();
        check(vdp->state().status == 0x1f && !vdp->state().spriteCount && !vdp->state().selectedSprites && !vdp->state().nextSpriteCount,
              "Reset clears sprite pipeline and flags");
    }
    // Confere os 32 sprites sem limite e preserva o comportamento original quando o limite está ligado.
    void unlimited()
    {
        auto defaults = std::make_unique<VDP>();
        check(defaults->state().spriteUnlimited, "Unlimited sprites is the emulator default");
        defaults->reset();
        check(defaults->state().spriteUnlimited, "Reset preserves the default unlimited preference");
        defaults->setSpriteUnlimited(false);
        defaults->reset();
        check(!defaults->state().spriteUnlimited, "Reset preserves an explicit hardware-limit preference");
        for (bool unlimited : {false, true})
        {
            auto vdp = scene();
            check(!vdp->state().spriteUnlimited, "Hardware scenes explicitly enable the four-sprite limit");
            vdp->setSpriteUnlimited(unlimited);
            solid(*vdp);
            for (unsigned i = 0; i < 32; ++i)
                sprite(*vdp, i, 0xff, static_cast<Byte>(i * 8), 0, 15);
            warm(*vdp);
            vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine);
            for (unsigned x = 0; x < 256; ++x)
                pixel(*vdp, x, 0, unlimited || x < 32 ? 1 : 0);
            check((vdp->state().status & 0x7f) == 0x44, "Fifth-sprite flag/index remain available in unlimited mode");
            const auto saved = std::make_unique<VDP::State>(vdp->state());
            vdp->setSpriteUnlimited(!unlimited);
            vdp->restore(*saved);
            check(vdp->state().spriteUnlimited == unlimited, "Snapshot preserves the sprite limit option");
        }
    }

    // Uma escrita antes do fetch entra no latch; uma escrita posterior só aparece na próxima busca.
    void fetchTiming()
    {
        auto vdp = scene();
        sprite(*vdp, 0, 0, 10, 0, 15);
        upload(*vdp, SAT + 4, {0xd0});
        warm(*vdp);
        vdp->runMasterTicks(258 * 2);
        upload(*vdp, Patterns, {0x80});
        const auto saved = std::make_unique<VDP::State>(vdp->state());
        vdp->runMasterTicks(2);
        check(vdp->state().nextSprites[0].left == 0x80, "Pattern fetch occurs at its reserved dot");
        upload(*vdp, Patterns, {0x40});
        vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine - 518);
        pixel(*vdp, 10, 1, 1);
        pixel(*vdp, 11, 1, 0);
        vdp->restore(*saved);
        upload(*vdp, Patterns, {0x40});
        vdp->runMasterTicks(VDP::Height * VDP::MasterTicksPerLine - 516);
        pixel(*vdp, 10, 1, 0);
        pixel(*vdp, 11, 1, 1);

        // A fase intermediária dos atributos também faz parte do snapshot.
        vdp = scene(3);
        vdp->setSpriteUnlimited(true);
        for (unsigned i = 0; i < 32; ++i)
            sprite(*vdp, i, 0xff, static_cast<Byte>(i * 4), 0, static_cast<Byte>(i % 15 + 1));
        solid(*vdp);
        warm(*vdp);
        const auto origin = std::make_unique<VDP::State>(vdp->state());
        for (unsigned phaseDot : {0u, 8u, 17u, 240u, 250u, 253u, 259u, 275u, 289u, 305u, 317u, 325u, 329u})
        {
            vdp->restore(*origin);
            vdp->runMasterTicks(phaseDot * 2 + 1);
            const auto phaseState = std::make_unique<VDP::State>(vdp->state());
            vdp->runMasterTicks(VDP::MasterTicksPerFrame);
            const auto expected = std::make_unique<VDP::State>(vdp->state());
            vdp->restore(*phaseState);
            vdp->runMasterTicks(VDP::MasterTicksPerFrame);
            check(vdp->state().images == expected->images && vdp->state().status == expected->status && vdp->state().dot == expected->dot &&
                      vdp->state().line == expected->line,
                  "Resume in a sprite fetch must preserve the frame and flags");
        }
    }
}

int main()
{
    try
    {
        geometry();
        collisions();
        offscreenCollisions();
        overflow();
        modes();
        snapshot();
        unlimited();
        fetchTiming();
        std::cout << "TMS9918 sprite geometry, SIZE/MAG, clipping, priority, collision, overflow and snapshots passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
