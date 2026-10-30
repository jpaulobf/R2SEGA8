#include "Core/VDP/TMS9918/TMS9918.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;
    using Byte = std::uint8_t;
    using Access = VDP::Access;

    // Identifica a propriedade temporal que divergiu da sequência esperada.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Programa um endereço ou solicita a pré-busca pela mesma porta usada pelo Z80.
    void address(VDP &vdp, unsigned value, bool writing = true)
    {
        vdp.writeControl(static_cast<Byte>(value));
        vdp.writeControl(static_cast<Byte>((value >> 8) | (writing ? 0x40 : 0)));
    }

    // Aguarda um pedido terminar, sem esconder clocks nas operações das portas.
    void finish(VDP &vdp)
    {
        for (unsigned ticks = 0; vdp.state().cpuAccessPending && ticks < 100; ++ticks)
            vdp.tick();
        check(!vdp.state().cpuAccessPending, "CPU request never reached a VRAM slot");
    }

    // Cria uma linha ativa com tabelas independentes e quatro sprites selecionados para a próxima linha.
    std::unique_ptr<VDP> scene(unsigned mode = 0, bool active = true)
    {
        auto vdp = std::make_unique<VDP>();
        auto s = std::make_unique<VDP::State>(vdp->state());
        s->registers = {static_cast<Byte>(mode == 2 ? 2 : 0),
                        static_cast<Byte>(active ? mode == 1   ? 0xd0
                                                   : mode == 4 ? 0xc8
                                                               : 0xc0
                                                 : 0x80),
                        6,
                        0x80,
                        0,
                        0x7f,
                        5,
                        0xf4};
        s->line = 20;
        s->spriteTargetLine = 21;
        auto &prefetch = s->background[mode == 1 ? 0 : 1];
        prefetch.named = true;
        prefetch.line = 20;
        prefetch.column = mode == 1 ? 0 : 1;
        prefetch.mode = static_cast<Byte>(mode == 8 ? 0 : mode);
        s->spriteEvaluationStopped = mode == 1 || !active;
        s->vram[0x3f80] = 0xd0;
        for (unsigned i = 0; i < 4; ++i)
        {
            s->selectedSprites++;
            s->evaluatedSprites[i].index = static_cast<Byte>(i);
            s->vram[0x3f80 + i * 4] = 20;
        }
        s->vram[0x3f90] = 0xd0;
        vdp->restore(*s);
        return vdp;
    }

    // Confere preparação, refresh, escrita adiada, pré-busca, incremento e volta de endereço.
    void ports()
    {
        auto vdp = std::make_unique<VDP>();
        address(*vdp, 0x3fff);
        vdp->writeData(0xa5);
        check(vdp->state().vram[0x3fff] == 0 && vdp->state().address == 0x3fff && vdp->state().readBuffer == 0xa5,
              "Port write updates the data latch but must not write VRAM or increment immediately");
        vdp->runMasterTicks(VDP::CPUAccessDelay - 1);
        check(vdp->state().cpuAccessPending, "CPU request cannot bypass its preparation delay");
        vdp->tick();
        check(vdp->state().access == Access::Refresh && vdp->state().cpuAccessPending, "Ready CPU request cannot take a refresh slot");
        vdp->runMasterTicks(3);
        check(vdp->state().vram[0x3fff] == 0, "Write cannot precede the next eligible dot");
        vdp->tick();
        check(vdp->state().access == Access::CPUWrite && vdp->state().accessAddress == 0x3fff && vdp->state().vram[0x3fff] == 0xa5 &&
                  vdp->state().address == 0,
              "Write must commit at the CPU slot and wrap the pointer");
        vdp->writeData(0x5a);
        finish(*vdp);
        address(*vdp, 0x3fff, false);
        check(vdp->state().address == 0x3fff && vdp->state().readBuffer == 0x5a && vdp->state().cpuAccessPending,
              "Read command must schedule its initial prefetch");
        finish(*vdp);
        check(vdp->state().readBuffer == 0xa5 && vdp->state().address == 0, "Prefetch completion wraps the pointer");
        check(vdp->readData() == 0xa5 && vdp->state().address == 0, "IN returns the previous buffer without advancing VRAM immediately");
        finish(*vdp);
        check(vdp->readData() == 0x5a && vdp->state().address == 1, "Sequential IN observes the preceding prefetch");
        vdp->reset();
        check(!vdp->state().cpuAccessPending && vdp->state().vram[0x3fff] == 0xa5, "Reset cancels a pending request and preserves VRAM");
    }

    // Exige a perda do pedido substituído, sem fila, novo atraso ou incremento adicional.
    void tooFast()
    {
        auto vdp = std::make_unique<VDP>();
        vdp->writeData(0x11);
        vdp->runMasterTicks(4);
        vdp->writeData(0x22);
        check(vdp->state().cpuAccessDelay == VDP::CPUAccessDelay - 4, "Overwriting a request must preserve its original delay");
        finish(*vdp);
        check(vdp->state().vram[0] == 0x22 && vdp->state().vram[1] == 0 && vdp->state().address == 1,
              "Too-fast writes replace one pending byte instead of silently queueing both");
        address(*vdp, 0, false);
        check(vdp->readData() == 0x22, "Too-fast IN returns the old data latch");
        vdp->writeData(0x33);
        finish(*vdp);
        check(vdp->state().vram[0] == 0x33 && vdp->state().address == 1,
              "A pending read can be replaced by a write without an extra pointer increment");
        vdp->writeData(0x44);
        vdp->writeControl(0x34);
        vdp->readStatus();
        finish(*vdp);
        check(vdp->state().vram[0x34] == 0x44 && !vdp->state().secondControlByte,
              "Status cancels the control sequence, not a pending access; the slot uses the current pointer");
        address(*vdp, 0xff);
        vdp->writeData(0x77);
        vdp->writeControl(0xff);
        finish(*vdp);
        vdp->writeControl(0x87);
        check(vdp->state().address == 0x700 && vdp->state().registers[7] == 0xff,
              "Register command must retain pointer increments occurring between its two bytes");
    }

    // Compara todos os slots de uma linha com os mapas publicados, nas coordenadas originais do diagrama.
    void slots()
    {
        constexpr unsigned gfx[] = {1, 3, 5, 7, 29, 31, 33, 35, 55, 87, 119, 151, 183, 215, 247, 279, 309, 311, 341};
        constexpr unsigned multicolor[] = {1, 3, 5, 7, 29, 31, 33, 35, 55, 57, 65, 73, 81, 87, 89, 97, 105,
                                           113, 119, 121, 129, 137, 145, 151, 153, 161, 169, 177, 183, 185, 193, 201, 209, 215,
                                           217, 225, 233, 241, 247, 249, 257, 265, 273, 279, 281, 289, 297, 305, 309, 311, 341};
        for (unsigned mode : {0u, 2u, 4u, 1u, 8u})
        {
            auto vdp = scene(mode, mode != 8);
            std::vector<unsigned> actual;
            unsigned refresh = 0, names = 0, colors = 0, patterns = 0;
            for (unsigned dot = 0; dot < VDP::DotsPerLine; ++dot)
            {
                // Mantém um pedido já preparado para observar todas as oportunidades de CPU.
                auto s = std::make_unique<VDP::State>(vdp->state());
                s->cpuAccessPending = s->cpuAccessWrite = true;
                s->cpuAccessDelay = 0;
                s->address = 0x3800;
                vdp->restore(*s);
                vdp->runMasterTicks(2);
                const auto owner = vdp->state().access;
                if (owner == Access::CPUWrite)
                    actual.push_back((dot + 63) % 342);
                if (owner == Access::Refresh)
                    ++refresh;
                if (owner == Access::Name)
                    ++names;
                if (owner == Access::Color)
                    ++colors;
                if (owner == Access::Pattern)
                    ++patterns;
                check(!(dot & 1) || owner == Access::None, "A memory slot occupies two dots, not every dot");
            }
            std::vector<unsigned> expected;
            if (mode == 0 || mode == 2)
                expected.assign(std::begin(gfx), std::end(gfx));
            else if (mode == 4)
                expected.assign(std::begin(multicolor), std::end(multicolor));
            else
                for (unsigned cycle = 1; cycle < 342; cycle += 2)
                {
                    const bool occupied = mode == 8 ? cycle >= 53 && cycle <= 305 && (cycle - 53) % 4 == 0
                                                    : (cycle >= 59 && cycle <= 293 && (cycle - 59) % 6 == 0) ||
                                                          (cycle >= 63 && cycle <= 297 && (cycle - 63) % 6 == 0);
                    if (!occupied)
                        expected.push_back(cycle);
                }
            // A origem visível começa no ciclo 63; compara como conjuntos sem ocultar duplicações.
            std::array<bool, 342> got{}, want{};
            for (auto cycle : actual)
            {
                check(!got[cycle], "Duplicate CPU slot");
                got[cycle] = true;
            }
            for (auto cycle : expected)
                want[cycle] = true;
            check(actual.size() == expected.size() && got == want, "CPU slot map differs from the reference timing diagram");
            check(mode == 8 ? refresh == 64
                            : names == (mode == 1 ? 40u : 32u) && patterns == names && colors == (mode == 0 || mode == 2 ? 32u : 0u),
                  "Background/refresh fetch counts differ from the selected mode");
        }
    }

    // Usa uma escrita real da CPU entre nome e cor/padrão para conferir os dados efetivamente apresentados.
    void backgroundRace()
    {
        for (unsigned change = 0; change < 3; ++change)
        {
            auto vdp = scene();
            auto initial = std::make_unique<VDP::State>(vdp->state());
            initial->vram[0x1800 + (20 / 8) * 32 + 4] = 1;
            initial->vram[8 + 4] = 0xff;
            initial->vram[16 + 4] = 0;
            initial->vram[0x2000] = 0xf4;
            vdp->restore(*initial);
            // Nome da coluna 4: dot 22; slot CPU: dot 24; cor: dot 26; padrão: dot 28.
            vdp->runMasterTicks(18 * 2);
            address(*vdp, change == 0 ? 0x1800 + (20 / 8) * 32 + 4 : change == 1 ? 12
                                                                                 : 0x2000);
            vdp->writeData(change == 0 ? 2 : change == 1 ? 0
                                                         : 0x24);
            vdp->runMasterTicks(10);
            check(vdp->state().access == Access::Name && vdp->state().accessData == 1, "Name fetch must precede color/pattern fetches");
            vdp->runMasterTicks(4);
            check(vdp->state().access == Access::CPUWrite, "CPU must own its slot between background reads");
            const auto saved = std::make_unique<VDP::State>(vdp->state());
            vdp->runMasterTicks((40 - 25) * 2);
            const auto expected = std::make_unique<VDP::State>(vdp->state());
            const auto &image = vdp->state().images[vdp->state().drawingImage];
            const auto *pixel = image.data() + (20 * 256 + 32) * 4;
            check(change == 0   ? pixel[0] == 255 && pixel[1] == 255
                  : change == 1 ? pixel[0] == 84 && pixel[1] == 85
                                : pixel[0] == 33 && pixel[1] == 200,
                  "Background must retain its fetched name and observe later color/pattern writes");
            vdp->restore(*saved);
            vdp->runMasterTicks((40 - 25) * 2);
            check(vdp->state().images == expected->images && vdp->state().pattern == expected->pattern &&
                      vdp->state().colors == expected->colors,
                  "Snapshot between name/color/pattern must resume the same output");
        }
    }

    // Retoma um pedido durante preparação e durante espera por slot; rejeita fases impossíveis.
    void snapshots()
    {
        auto vdp = scene();
        for (unsigned elapsed : {1u, 13u, 17u, 33u})
        {
            vdp = scene();
            address(*vdp, 0x3800);
            vdp->writeData(0xa5);
            vdp->runMasterTicks(elapsed);
            const auto saved = std::make_unique<VDP::State>(vdp->state());
            vdp->runMasterTicks(100);
            const auto expected = std::make_unique<VDP::State>(vdp->state());
            vdp->restore(*saved);
            vdp->runMasterTicks(100);
            check(vdp->state().vram == expected->vram && vdp->state().address == expected->address &&
                      vdp->state().cpuAccessPending == expected->cpuAccessPending && vdp->state().images == expected->images,
                  "Pending CPU transfer did not resume exactly from snapshot");
        }
        auto invalid = std::make_unique<VDP::State>(vdp->state());
        invalid->cpuAccessDelay = VDP::CPUAccessDelay + 1;
        bool rejected = false;
        try
        {
            vdp->restore(*invalid);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid CPU preparation phase accepted");
    }
} // namespace

int main()
{
    try
    {
        ports();
        tooFast();
        slots();
        backgroundRace();
        snapshots();
        std::cout << "TMS9918 VRAM arbitration, CPU delay/overwrite, slot maps, background fetch races and snapshots passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}