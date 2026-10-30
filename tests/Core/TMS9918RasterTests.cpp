#include "Core/VDP/TMS9918/TMS9918.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using VDP = R2SEGA8::Core::TMS9918;
    using Byte = std::uint8_t;

    // Identifica uma diferença na aplicação temporal dos registradores.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Conclui uma escrita de registrador sem adiantar o clock do VDP.
    void reg(VDP &vdp, unsigned index, unsigned value)
    {
        vdp.writeControl(static_cast<Byte>(value));
        vdp.writeControl(static_cast<Byte>(0x80 | index));
    }

    // Posiciona o teste antes de um dot ou entre os dois ciclos do cristal desse dot.
    void seek(VDP &vdp, unsigned line, unsigned dot, unsigned phase = 0)
    {
        const unsigned current = (vdp.state().line * 342 + vdp.state().dot) * 2 + vdp.state().masterPhase;
        const unsigned target = (line * 342 + dot) * 2 + phase;
        vdp.runMasterTicks((target + VDP::MasterTicksPerFrame - current) % VDP::MasterTicksPerFrame);
    }

    // Prepara uma cena independente: nomes 1, padrão AA, cores F4 e primeira célula pré-buscada.
    std::unique_ptr<VDP> scene(unsigned mode = 0)
    {
        auto vdp = std::make_unique<VDP>();
        auto s = std::make_unique<VDP::State>(vdp->state());
        s->line = 20;
        s->registers = {0, static_cast<Byte>(mode == 1 ? 0xd0 : 0xc0), 6, 0x80, 1, 0x7f, 5, 0xf4};
        std::fill_n(s->vram.begin() + 0x1800, 960, 1);
        std::fill_n(s->vram.begin() + 0x1c00, 960, 2);
        std::fill_n(s->vram.begin() + 0x808, 8, 0xaa);
        std::fill_n(s->vram.begin() + 0x818, 8, 0xff);
        std::fill_n(s->vram.begin() + 0x1008, 8, 0xf0);
        s->vram[0x2000] = 0xf4;
        s->vram[0x2400] = 0x26;
        s->vram[0x3f80] = 0xd0;
        for (unsigned i = 0; i < 2; ++i)
        {
            auto &cell = s->background[i];
            cell.name = 1;
            cell.pattern = 0xaa;
            cell.colors = 0xf4;
            cell.column = static_cast<Byte>(i);
            cell.mode = static_cast<Byte>(mode);
            cell.line = 20;
            cell.named = true;
            cell.ready = i == 0;
        }
        vdp->restore(*s);
        return vdp;
    }

    // Confere diretamente o quadro em construção para observar o pixel logo após seu dot.
    void pixel(const VDP &vdp, unsigned x, unsigned y, unsigned color)
    {
        constexpr Byte rgb[][3] = {{84, 85, 237}, {255, 255, 255}, {33, 200, 66}, {212, 82, 77}};
        const auto &s = vdp.state();
        const auto *p = s.images[s.drawingImage].data() + (y * 256 + x) * 4;
        check(p[0] == rgb[color][0] && p[1] == rgb[color][1] && p[2] == rgb[color][2] && p[3] == 255,
              "Register raster change affected the wrong pixel or reinterpreted a stale latch");
    }

    // Verifica R7 no dot seguinte, inclusive escrita parcial de controle e mudança no meio de um caractere.
    void colors()
    {
        auto vdp = scene();
        auto s = std::make_unique<VDP::State>(vdp->state());
        s->registers[1] = 0x80;
        vdp->restore(*s);
        seek(*vdp, 20, 10, 1);
        reg(*vdp, 7, 6);
        vdp->tick();
        pixel(*vdp, 9, 20, 0);
        pixel(*vdp, 10, 20, 3);
        vdp->writeControl(2);
        vdp->runMasterTicks(4);
        pixel(*vdp, 12, 20, 3);
        vdp->writeControl(0x87);
        vdp->runMasterTicks(2);
        pixel(*vdp, 13, 20, 2);
        pixel(*vdp, 9, 20, 0);

        vdp = scene(1);
        seek(*vdp, 20, 8);
        reg(*vdp, 7, 0x26);
        vdp->runMasterTicks(4);
        pixel(*vdp, 6, 20, 1);
        pixel(*vdp, 7, 20, 0);
        pixel(*vdp, 8, 20, 2);
        pixel(*vdp, 9, 20, 3);
    }

    // Apagamento corta a saída, mas não congela o posicionamento dos latches já pré-buscados.
    void blanking()
    {
        for (unsigned resume : {34u, 43u})
        {
            auto vdp = scene();
            auto s = std::make_unique<VDP::State>(vdp->state());
            s->vram[0x1800 + 2 * 32 + 3] = 2; // Célula anterior vazia para detectar um latch não atualizado.
            s->vram[0x1800 + 2 * 32 + 5] = 3;
            s->vram[0x1800 + 2 * 32 + 7] = 3;
            vdp->restore(*s);
            seek(*vdp, 20, 31);
            reg(*vdp, 1, 0x80);
            seek(*vdp, 20, resume);
            reg(*vdp, 1, 0xc0);
            seek(*vdp, 20, 64);
            pixel(*vdp, 32, 20, 0);
            pixel(*vdp, 33, 20, 0);
            if (resume == 34)
            {
                pixel(*vdp, 34, 20, 1);
                pixel(*vdp, 35, 20, 0);
                pixel(*vdp, 40, 20, 1);
            }
            else
            {
                for (unsigned x = 43; x < 56; ++x)
                    pixel(*vdp, x, 20, 0);
                pixel(*vdp, 56, 20, 1);
            }
        }
    }

    // R2/R3/R4 alteram somente as buscas posteriores; bytes já lidos ficam preservados na célula.
    void tables()
    {
        for (unsigned writeDot : {22u, 23u})
        {
            auto vdp = scene();
            seek(*vdp, 20, writeDot);
            reg(*vdp, 2, 7);
            seek(*vdp, 20, 48);
            pixel(*vdp, 32, 20, writeDot == 22 ? 0 : 1);
            pixel(*vdp, 40, 20, 0);
        }
        for (unsigned writeDot : {26u, 27u})
        {
            auto vdp = scene();
            seek(*vdp, 20, writeDot);
            reg(*vdp, 3, 0x90);
            seek(*vdp, 20, 48);
            pixel(*vdp, 32, 20, writeDot == 26 ? 2 : 1);
            pixel(*vdp, 33, 20, writeDot == 26 ? 3 : 0);
            pixel(*vdp, 40, 20, 2);
        }
        for (unsigned writeDot : {28u, 29u})
        {
            auto vdp = scene();
            seek(*vdp, 20, writeDot);
            reg(*vdp, 4, 2);
            seek(*vdp, 20, 48);
            pixel(*vdp, 33, 20, writeDot == 28 ? 1 : 0);
            pixel(*vdp, 36, 20, writeDot == 28 ? 0 : 1);
            pixel(*vdp, 41, 20, 1);
        }
    }

    // Em Graphics II, R3 pode mudar a cor antes ou depois do fetch e também mascarar o padrão posterior.
    void maskedTables()
    {
        for (unsigned writeDot : {26u, 27u, 29u})
        {
            auto vdp = scene();
            auto s = std::make_unique<VDP::State>(vdp->state());
            s->registers[0] = 2;
            s->registers[3] = 0xff;
            s->registers[4] = 3;
            std::fill_n(s->vram.begin() + 0x1800, 768, 8);
            for (auto &cell : s->background)
            {
                cell.mode = 2;
                cell.name = 8;
            }
            s->vram[0x44] = 0xaa;
            s->vram[4] = 0xf0;
            s->vram[0x2044] = 0xf4;
            s->vram[0x2004] = 0x26;
            vdp->restore(*s);
            seek(*vdp, 20, writeDot);
            reg(*vdp, 3, 0x80);
            seek(*vdp, 20, 48);
            pixel(*vdp, 32, 20, writeDot == 26 ? 2 : 1);
            pixel(*vdp, 33, 20, writeDot == 26 ? 2 : writeDot == 27 ? 1
                                                                    : 0);
            pixel(*vdp, 36, 20, writeDot == 26 ? 3 : writeDot == 27 ? 0
                                                                    : 1);
            pixel(*vdp, 40, 20, 2);
        }
    }

    // Troca o modo durante uma célula; a saída termina no formato antigo e aguarda dados válidos do novo.
    void modes()
    {
        for (unsigned next : {1u, 2u, 4u})
        {
            auto vdp = scene();
            auto s = std::make_unique<VDP::State>(vdp->state());
            s->vram[0x80d] = 0x26; // Par de cores usado pelo modo Multicolor na linha 20.
            s->vram[12] = 0xf0;
            s->vram[0x200c] = 0x26;
            vdp->restore(*s);
            seek(*vdp, 20, 35);
            if (next == 2)
                reg(*vdp, 0, 2);
            else
                reg(*vdp, 1, next == 1 ? 0xd0 : 0xc8);
            check(vdp->state().outputMode == 0, "Mode write must not reinterpret the loaded cell");
            const auto saved = std::make_unique<VDP::State>(vdp->state());
            seek(*vdp, 20, 60);
            pixel(*vdp, 34, 20, 1);
            pixel(*vdp, 35, 20, 0);
            pixel(*vdp, 36, 20, 1);
            for (unsigned x = 40; x < 48; ++x)
                pixel(*vdp, x, 20, 0);
            pixel(*vdp, 48, 20, next == 1 ? 1 : 2);
            pixel(*vdp, 52, 20, next == 1 ? 1 : 3);
            const auto expected = std::make_unique<VDP::State>(vdp->state());
            vdp->restore(*saved);
            seek(*vdp, 20, 60);
            check(vdp->state().images == expected->images && vdp->state().outputMode == expected->outputMode &&
                      vdp->state().outputStart == expected->outputStart && vdp->state().outputValid == expected->outputValid,
                  "Snapshot between mode write and cell completion must retain both timing domains");
        }
        auto vdp = scene(1);
        seek(*vdp, 20, 11);
        reg(*vdp, 1, 0xc0);
        seek(*vdp, 20, 32);
        pixel(*vdp, 10, 20, 1);
        pixel(*vdp, 11, 20, 0);
        for (unsigned x = 12; x < 24; ++x)
            pixel(*vdp, x, 20, 0);
        pixel(*vdp, 24, 20, 1);
    }

    // Prepara um sprite selecionado para a próxima linha sem alterar os slots reais de atributos/padrões.
    std::unique_ptr<VDP> spriteScene(unsigned options = 0)
    {
        auto vdp = scene();
        auto s = std::make_unique<VDP::State>(vdp->state());
        s->registers[1] = static_cast<Byte>(0xc0 | options);
        s->spriteTargetLine = 21;
        s->selectedSprites = 1;
        s->evaluatedSprites[0].index = 0;
        s->vram[0x3f80] = 20;
        s->vram[0x3f81] = 10;
        s->vram[0x3f83] = 15;
        s->vram[0x3f84] = 0xd0;
        s->vram[0x2800] = 0xff;
        s->vram[0x2810] = 0xff;
        s->vram[0x3010] = 0x80;
        s->vram[0x3e00] = 100;
        s->vram[0x3e01] = 40;
        s->vram[0x3e02] = 0;
        s->vram[0x3e03] = 2;
        s->registers[3] = 0; // Fundo transparente para destacar os sprites em todas as posições.
        vdp->restore(*s);
        return vdp;
    }

    // Confere R5/R6 por fase e SIZE/MAG retidos no fetch Y, sem alterar sprites já em apresentação.
    void spriteRegisters()
    {
        auto vdp = spriteScene(2);
        seek(*vdp, 20, 259);
        reg(*vdp, 6, 6);
        seek(*vdp, 21, 30);
        pixel(*vdp, 10, 21, 1);
        pixel(*vdp, 17, 21, 1);
        pixel(*vdp, 18, 21, 1);
        pixel(*vdp, 20, 21, 0);

        vdp = spriteScene();
        seek(*vdp, 20, 251);
        reg(*vdp, 5, 0x7c);
        seek(*vdp, 21, 50);
        pixel(*vdp, 10, 21, 0);
        pixel(*vdp, 40, 21, 2);

        for (unsigned writeDot : {250u, 251u})
        {
            vdp = spriteScene();
            seek(*vdp, 20, writeDot);
            reg(*vdp, 1, 0xc3);
            seek(*vdp, 21, 12);
            reg(*vdp, 6, 6);
            seek(*vdp, 21, 45);
            pixel(*vdp, 10, 21, 1);
            pixel(*vdp, 17, 21, 1);
            pixel(*vdp, 18, 21, writeDot == 250 ? 1 : 0);
            pixel(*vdp, 41, 21, writeDot == 250 ? 1 : 0);
            pixel(*vdp, 42, 21, 0);
        }
    }

    // Apagamento e Text ocultam os sprites e não criam colisões enquanto a saída está desativada.
    void spriteGating()
    {
        auto vdp = spriteScene();
        seek(*vdp, 20, 8);
        auto s = std::make_unique<VDP::State>(vdp->state());
        s->spriteCount = 2;
        for (unsigned i = 0; i < 2; ++i)
        {
            s->sprites[i].visible = true;
            s->sprites[i].x = 10;
            s->sprites[i].left = 0xff;
            s->sprites[i].attributes = i == 0 ? 15 : 2;
        }
        s->status &= 0x1f;
        vdp->restore(*s);
        seek(*vdp, 20, 10);
        reg(*vdp, 1, 0x80);
        seek(*vdp, 20, 12);
        pixel(*vdp, 10, 20, 0);
        check(!(vdp->state().status & 0x20), "Blanking must suppress sprite coincidence generation");
        reg(*vdp, 1, 0xc0);
        seek(*vdp, 20, 14);
        pixel(*vdp, 12, 20, 1);
        check(vdp->readStatus() & 0x20, "Resuming sprite output must use its preserved patterns and detect overlap");
        reg(*vdp, 1, 0xd0);
        seek(*vdp, 20, 16);
        pixel(*vdp, 14, 20, 0);
        check(!(vdp->state().status & 0x20), "Text must suppress sprite coincidence generation");
        reg(*vdp, 1, 0xc0);
        vdp->runMasterTicks(2);
        pixel(*vdp, 16, 20, 1);
        check(vdp->state().status & 0x20, "Returning to graphics can reuse the sprite data already fetched for this line");
    }

    // A mudança de modo/apagamento altera os próximos slots, preservando o prazo do pedido da CPU.
    void pendingTransfer()
    {
        for (bool blank : {false, true})
        {
            auto vdp = scene();
            vdp->writeControl(0);
            vdp->writeControl(0x78);
            vdp->writeData(0xa5);
            seek(*vdp, 20, 5);
            reg(*vdp, 1, blank ? 0x80 : 0xc8);
            check(vdp->state().cpuAccessDelay == 4, "Register changes must not restart CPU preparation");
            // Escrever R1 também muda o ponteiro do TMS99xxA: o slot deve usar esse endereço atual.
            const auto target = vdp->state().address;
            vdp->runMasterTicks(blank ? 8 : 12);
            check(!vdp->state().cpuAccessPending && vdp->state().access == VDP::Access::CPUWrite && vdp->state().vram[target] == 0xa5 &&
                      vdp->state().address == ((target + 1) & 0x3fff),
                  "Pending transfer must use the newly available slot without losing its data or pointer effects");
        }
        auto vdp = scene();
        auto invalid = std::make_unique<VDP::State>(vdp->state());
        invalid->outputValid = true;
        invalid->outputWidth = 0;
        bool rejected = false;
        try
        {
            vdp->restore(*invalid);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid active output cell accepted");
    }
} // namespace

int main()
{
    try
    {
        colors();
        blanking();
        tables();
        maskedTables();
        modes();
        spriteRegisters();
        spriteGating();
        pendingTransfer();
        std::cout << "TMS9918 raster register changes, mode/output latches, blanking, sprite phases and pending transfers passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}