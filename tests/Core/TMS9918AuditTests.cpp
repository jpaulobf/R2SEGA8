#include "Core/VDP/TMS9918/TMS9918.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;
    using Byte = std::uint8_t;

    // Relata a propriedade e o cenário que produziram uma divergência.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Usa as portas de controle para alterar IE sem reconhecer o status pendente.
    void reg(VDP &vdp, unsigned index, Byte value)
    {
        vdp.writeControl(value);
        vdp.writeControl(static_cast<Byte>(0x80 | index));
    }

    // Confere todos os campos sem comparar padding ou depender da representação binária das estruturas.
    bool sameState(const VDP::State &a, const VDP::State &b)
    {
        if (a.vram != b.vram || a.registers != b.registers || a.images != b.images)
            return false;
        for (unsigned i = 0; i < a.background.size(); ++i)
        {
            const auto &x = a.background[i];
            const auto &y = b.background[i];
            if (x.name != y.name || x.pattern != y.pattern || x.colors != y.colors || x.column != y.column || x.mode != y.mode ||
                x.line != y.line || x.named != y.named || x.ready != y.ready)
                return false;
        }
        for (unsigned bank = 0; bank < 3; ++bank)
        {
            const auto &left = bank == 0 ? a.evaluatedSprites : bank == 1 ? a.nextSprites
                                                                          : a.sprites;
            const auto &right = bank == 0 ? b.evaluatedSprites : bank == 1 ? b.nextSprites
                                                                           : b.sprites;
            for (unsigned i = 0; i < left.size(); ++i)
            {
                const auto &x = left[i];
                const auto &y = right[i];
                if (x.index != y.index || x.y != y.y || x.x != y.x || x.name != y.name || x.attributes != y.attributes ||
                    x.left != y.left || x.right != y.right || x.row != y.row || x.size != y.size || x.scale != y.scale ||
                    x.visible != y.visible)
                    return false;
            }
        }
        return a.outputMode == b.outputMode && a.outputStart == b.outputStart && a.outputWidth == b.outputWidth &&
               a.outputLine == b.outputLine && a.outputValid == b.outputValid && a.cpuAccessPending == b.cpuAccessPending &&
               a.cpuAccessWrite == b.cpuAccessWrite && a.cpuAccessData == b.cpuAccessData && a.cpuAccessDelay == b.cpuAccessDelay &&
               a.refreshRow == b.refreshRow && a.access == b.access && a.accessAddress == b.accessAddress && a.accessData == b.accessData &&
               a.selectedSprites == b.selectedSprites && a.nextSpriteCount == b.nextSpriteCount && a.spriteCount == b.spriteCount &&
               a.extraFetchIndex == b.extraFetchIndex && a.extraFetchPhase == b.extraFetchPhase &&
               a.spriteTargetLine == b.spriteTargetLine && a.spriteEvaluationStopped == b.spriteEvaluationStopped &&
               a.spriteUnlimited == b.spriteUnlimited && a.evaluationSpriteUnlimited == b.evaluationSpriteUnlimited &&
               a.address == b.address && a.dot == b.dot && a.line == b.line && a.readBuffer == b.readBuffer &&
               a.controlLatch == b.controlLatch && a.status == b.status && a.masterPhase == b.masterPhase &&
               a.drawingImage == b.drawingImage && a.pattern == b.pattern && a.colors == b.colors &&
               a.secondControlByte == b.secondControlByte && a.masterTicks == b.masterTicks && a.completedFrames == b.completedFrames;
    }

    // Aplica a tabela lógica de F/IE e o reconhecimento de F/C/5S a todas as 256 combinações do status.
    void statusTruthTable()
    {
        auto vdp = std::make_unique<VDP>();
        auto state = std::make_unique<VDP::State>(vdp->state());
        for (bool unlimited : {false, true})
            for (unsigned value = 0; value < 256; ++value)
            {
                state->spriteUnlimited = unlimited;
                state->status = static_cast<Byte>(value);
                state->registers[1] = 0;
                state->secondControlByte = true;
                state->controlLatch = 0x76;
                state->address = 0x1234;
                state->readBuffer = 0x92;
                state->cpuAccessPending = true;
                state->cpuAccessWrite = true;
                state->cpuAccessData = 0x5a;
                state->cpuAccessDelay = 9;
                vdp->restore(*state);
                check(!vdp->interrupt(), "F cannot assert INT with IE disabled");
                // A leitura encerra uma escrita de controle parcial, mas não cancela o acesso à VRAM.
                check(vdp->readStatus() == value, "Status read must return all original bits");
                check(vdp->state().status == (value & 0x1f) && !vdp->state().secondControlByte,
                      "Status read clears F/C/5S and the partial control word, preserving the sprite index");
                check(vdp->state().address == 0x1234 && vdp->state().readBuffer == 0x92 && vdp->state().cpuAccessPending &&
                          vdp->state().cpuAccessWrite && vdp->state().cpuAccessData == 0x5a && vdp->state().cpuAccessDelay == 9 &&
                          vdp->state().masterTicks == 0,
                      "Reading status must preserve the VRAM transaction and clock");
                check(vdp->readStatus() == (value & 0x1f), "A repeated status read cannot restore acknowledged flags");

                state->secondControlByte = false;
                vdp->restore(*state);
                reg(*vdp, 1, 0x20);
                check(vdp->interrupt() == ((value & 0x80) != 0), "Only F and IE together assert INT; C/5S do not");
                reg(*vdp, 1, 0);
                check(!vdp->interrupt() && vdp->state().status == value, "Disabling IE does not acknowledge F/C/5S");
                reg(*vdp, 1, 0x20);
                vdp->readStatus();
                check(!vdp->interrupt(), "Acknowledging F deasserts INT with IE still enabled");
            }
    }

    // Restaura cada meio-dot de uma linha, com acessos pendentes, seleção e buscas nos quatro modos oficiais.
    void snapshotPhases()
    {
        auto original = std::make_unique<VDP>();
        auto resumed = std::make_unique<VDP>();
        auto state = std::make_unique<VDP::State>();
        for (unsigned mode : {0u, 1u, 2u, 4u})
            for (bool unlimited : {false, true})
            {
                original->reset();
                *state = original->state();
                state->registers = {static_cast<Byte>(mode == 2 ? 2 : 0),
                                    static_cast<Byte>(0xe2 | (mode == 1 ? 0x10 : mode == 4 ? 8
                                                                                           : 0)),
                                    6,
                                    0xff,
                                    3,
                                    0x7f,
                                    5,
                                    0xf4};
                state->spriteUnlimited = unlimited;
                for (unsigned i = 0; i < state->vram.size(); ++i)
                    state->vram[i] = static_cast<Byte>((i * 37) ^ (i >> 4));
                for (unsigned i = 0; i < 32; ++i)
                {
                    state->vram[0x3f80 + i * 4] = 20;
                    state->vram[0x3f81 + i * 4] = static_cast<Byte>(i * 8);
                    state->vram[0x3f82 + i * 4] = static_cast<Byte>(i * 4);
                    state->vram[0x3f83 + i * 4] = static_cast<Byte>(i | 0x80);
                }
                original->restore(*state);
                original->runMasterTicks(VDP::MasterTicksPerFrame + 30 * VDP::MasterTicksPerLine);
                for (unsigned phase = 0; phase < VDP::MasterTicksPerLine; ++phase)
                {
                    if (phase % 71 == 0)
                        original->writeData(static_cast<Byte>(phase));
                    if (phase % 113 == 0)
                        original->writeControl(static_cast<Byte>(phase));
                    *state = original->state();
                    resumed->restore(*state);
                    original->tick();
                    resumed->tick();
                    if (!sameState(original->state(), resumed->state()))
                        throw std::runtime_error("Snapshot divergence: mode=" + std::to_string(mode) +
                                                 " unlimited=" + std::to_string(unlimited) + " phase=" + std::to_string(phase));
                }
            }
    }

    // Um estado restaurável com seleção saturada não pode escrever fora do banco de 32 sprites.
    void saturatedSelection()
    {
        auto vdp = std::make_unique<VDP>();
        auto state = std::make_unique<VDP::State>(vdp->state());
        state->dot = 8;
        state->registers[1] = 0xc0;
        state->spriteTargetLine = 1;
        state->selectedSprites = 32;
        state->extraFetchIndex = 32;
        state->spriteEvaluationStopped = false;
        state->evaluationSpriteUnlimited = true;
        vdp->restore(*state);
        vdp->runMasterTicks(2);
        check(vdp->state().selectedSprites == 32, "Restored saturated selection must not append a 33rd sprite");
        check(vdp->state().nextSprites[0].index == 31, "Saturated selection must not overwrite the adjacent fetch bank");
    }
}

int main(int argc, char **argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "all";
        check(group == "all" || group == "status" || group == "snapshot", "Unknown TMS9918 audit group");
        if (group == "all" || group == "status")
            statusTruthTable();
        if (group == "all" || group == "snapshot")
        {
            snapshotPhases();
            saturatedSelection();
        }
        std::cout << "TMS9918 audit " << group << " passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}