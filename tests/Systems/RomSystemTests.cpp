#include "Common/RomFile.h"
#include "Systems/BoardFactory.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <class Action>
    void rejects(Action action, const char *message)
    {
        bool rejected = false;
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, message);
    }

    // Cria arquivos de teste isolados e remove somente os recursos criados pela instância.
    struct Fixtures
    {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files;
        // Cria uma pasta temporária exclusiva para os arquivos deste teste.
        Fixtures()
        {
            directory = std::filesystem::temp_directory_path() /
                        ("r2sega8-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            check(std::filesystem::create_directory(directory), "Unable to create fixture directory");
        }
        // Remove os arquivos registrados e, em seguida, a pasta temporária vazia.
        ~Fixtures()
        {
            std::error_code error;
            for (const auto &path : files)
                std::filesystem::remove(path, error);
            std::filesystem::remove(directory, error);
        }
        // Grava uma imagem de teste e registra seu caminho para limpeza ao encerrar.
        std::filesystem::path write(const std::string &name, const std::vector<std::uint8_t> &bytes)
        {
            const auto path = directory / std::filesystem::u8path(name);
            files.push_back(path);
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            check(static_cast<bool>(file), "Unable to write fixture");
            return path;
        }
    };
}

int main()
{
    try
    {
        using namespace R2SEGA8;
        using Common::SystemType;
        Fixtures fixtures;
        std::vector<std::uint8_t> bytes(0xc000);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>((i * 13 + i / 256) & 255);
        const auto sgPath = fixtures.write("fixture.SG", bytes);
        auto board = Systems::createBoard(SystemType::SG1000, Core::Cartridge(sgPath));
        check(board->cartridge().rom() == bytes, "ROM bytes changed on load");
        check(board->cartridge().suggestedSystem() == SystemType::SG1000, "Case-insensitive extension hint");
        check(board->ram().size() == 1024, "SG RAM size");
        for (unsigned address = 0; address < 0xc000; ++address)
            check(board->readMemory(static_cast<std::uint16_t>(address)) == bytes[address], "SG ROM mapping");
        for (unsigned i = 0; i < 1024; ++i)
            board->writeMemory(static_cast<std::uint16_t>(0xc000 + i), static_cast<std::uint8_t>(i * 7));
        for (unsigned address = 0xc000; address <= 0xffff; ++address)
            check(board->readMemory(static_cast<std::uint16_t>(address)) == static_cast<std::uint8_t>((address & 1023) * 7),
                  "SG RAM must mirror through FFFF");
        board->writeMemory(0, 0x99);
        check(board->readMemory(0) == bytes[0], "Writes must not change ROM");

        // O perfil depende do conteúdo catalogado, nunca apenas do nome ou dos 32 KiB de tamanho.
        std::vector<std::uint8_t> castleBytes(bytes.begin(), bytes.begin() + 0x8000);
        const auto castlePath = fixtures.write("Castle, The (Japan).sg", castleBytes);
        auto plain32 = Systems::createBoard(SystemType::SG1000, Core::Cartridge(castlePath));
        check(plain32->cartridge().profile() == Core::CartridgeProfile::PlainROM && plain32->cartridge().ram().empty(),
              "Filename/size must not enable cartridge RAM");
        plain32->writeMemory(0x8000, 0x42);
        check(plain32->readMemory(0x8000) == 0xff, "Unknown 32 KiB ROM acquired writable memory");

        // ROM sintética: o perfil explícito exercita o hardware sem distribuir o jogo comercial.
        auto castle = Systems::createBoard(SystemType::SG1000, Core::Cartridge(castlePath, Core::CartridgeProfile::CastleRAM));
        check(castle->ram().size() == 1024 && castle->cartridge().ram().size() == 8192, "Separate internal and cartridge RAM");
        check(std::all_of(castle->cartridge().ram().begin(), castle->cartridge().ram().end(), [](auto v)
                          { return v == 0; }),
              "Cartridge RAM power-on convention");
        for (unsigned i = 0; i < 8192; ++i)
            castle->writeMemory(static_cast<std::uint16_t>(0x8000 + i), static_cast<std::uint8_t>(i * 7 + i / 256));
        for (unsigned i = 0; i < 8192; ++i)
            check(castle->readMemory(static_cast<std::uint16_t>(0x8000 + i)) == static_cast<std::uint8_t>(i * 7 + i / 256),
                  "All 8 KiB of cartridge RAM must be independently addressable");
        castle->writeMemory(0x7fff, 0x77);
        castle->writeMemory(0xa000, 0x77);
        castle->writeMemory(0xbfff, 0x77);
        check(castle->readMemory(0x7fff) == castleBytes[0x7fff] && castle->readMemory(0xa000) == castleBytes[0x2000] &&
                  castle->readMemory(0xbfff) == castleBytes[0x3fff] && castle->cartridge().rom() == castleBytes,
              "Castle ROM boundaries/mirror must remain read-only");
        castle->writeMemory(0xc000, 0x53);
        check(castle->readMemory(0xfc00) == 0x53 && castle->readMemory(0x8000) == 0 && castle->readMemory(0x8400) == 4,
              "Internal RAM must not alias cartridge RAM");
        castle->cpu().reset();
        check(castle->readMemory(0xc000) == 0x53 && castle->readMemory(0x9fff) == static_cast<std::uint8_t>(8191 * 7 + 31),
              "CPU reset must preserve cartridge RAM");
        auto freshCastle = Systems::createBoard(SystemType::SG1000, Core::Cartridge(castlePath, Core::CartridgeProfile::CastleRAM));
        check(freshCastle->readMemory(0x9fff) == 0, "Reload must create a separate cartridge RAM allocation");
        rejects([&]
                { Core::Cartridge invalid(sgPath, Core::CartridgeProfile::CastleRAM); }, "Castle profile accepted a non-32 KiB ROM");

        // A CPU deve buscar instruções e usar a pilha na RAM do cartucho pelo barramento real.
        std::vector<std::uint8_t> ramProgram(0x8000);
        const std::vector<std::uint8_t> code = {0x31, 0x00, 0xa0, 0x21, 0x76, 0x00, 0xe5, 0xd1, 0x7b, 0x32, 0x00, 0x80, 0xc3, 0x00, 0x80};
        std::copy(code.begin(), code.end(), ramProgram.begin());
        auto ramCPU = Systems::createBoard(
            SystemType::SG1000, Core::Cartridge(fixtures.write("cart-ram-program.sg", ramProgram), Core::CartridgeProfile::CastleRAM));
        ramCPU->runCycles(200);
        check(ramCPU->cpu().state().halted && ramCPU->cpu().state().pc == 0x8001 && ramCPU->readMemory(0x8000) == 0x76 &&
                  ramCPU->cpu().state().e == 0x76 && ramCPU->cpu().state().sp == 0xa000,
              "CPU stack, writes and instruction fetch must use cartridge RAM");
        auto *previous = board.get();
        rejects([&]
                { board = Systems::createBoard(SystemType::Unknown, Core::Cartridge(sgPath)); }, "Unknown system accepted");
        check(board.get() == previous, "Failed load replaced the active machine");
        const auto banked = fixtures.write("banked.sg", std::vector<std::uint8_t>(0x10000, 1));
        rejects([&]
                { board = Systems::createBoard(SystemType::SG1000, Core::Cartridge(banked)); }, "Oversized SG plain ROM accepted");
        check(board.get() == previous && board->readMemory(0xc007) == 49, "Failed replacement damaged RAM");

        const auto small = fixtures.write("small.bin", {0x12, 0x34, 0x56});
        auto smallBoard = Systems::createBoard(SystemType::SG1000, Core::Cartridge(small));
        check(smallBoard->readMemory(2) == 0x56 && smallBoard->readMemory(3) == 0xff, "Unpopulated ROM handling");
        check(smallBoard->cartridge().suggestedSystem() == SystemType::Unknown, "Unknown extension guessed a system");
        const auto cpuProgram = fixtures.write("cpu.sg", {0x31, 0x00, 0xc4, 0x3e, 0x42, 0x32, 0x05, 0xc0, 0x76});
        auto executing = Systems::createBoard(SystemType::SG1000, Core::Cartridge(cpuProgram));
        check(executing->clockFrequency() == 3579545 && executing->runCycles(34) == 34, "SG CPU clock/budget");
        check(executing->cpu().state().halted && executing->cpu().state().sp == 0xc400 &&
                  executing->readMemory(0xfc05) == 0x42,
              "SG program writes mirrored RAM through real CPU bus");
        check(executing->runCycles(5) == 5 && executing->cpu().state().cycles == 39, "HALT respects an exact T-state budget");
        check(executing->input(0xdc) == 0xff, "Unconnected controller ports return FF");
        executing->output(0xdc, 0x55);
        executing->cpu().reset();
        check(executing->cpu().state().pc == 0 && executing->readMemory(0xc005) == 0x42, "CPU reset does not clear system RAM");
        check(std::all_of(smallBoard->ram().begin(), smallBoard->ram().end(), [](auto value)
                          { return value == 0; }),
              "RAM initialization");
        const auto smsProgram = fixtures.write("cpu.sms", {0x31, 0x00, 0xe0, 0x3e, 0x42, 0x32, 0x05, 0xc0, 0x76});
        auto sms = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(smsProgram));
        check(sms->hasMemoryMap() && sms->cyclesPerFrame() == 59736 && sms->clockFrequency() == 3579545, "SMS mapping or scheduling");
        const auto europeanPath = fixtures.write("fixture (Europe).sms", std::vector<std::uint8_t>(0x8000));
        auto autoPal = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(europeanPath));
        auto forcedNtsc = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(europeanPath), Common::ConsoleProfile::NTSC);
        auto forcedPal = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(smsProgram), Common::ConsoleProfile::PAL);
        check(autoPal->cyclesPerFrame() == 71364 && autoPal->clockFrequency() == 3546893, "Auto must select PAL for Europe");
        check(forcedNtsc->cyclesPerFrame() == 59736 && forcedNtsc->clockFrequency() == 3579545, "NTSC override");
        check(forcedPal->cyclesPerFrame() == 71364 && forcedPal->clockFrequency() == 3546893, "PAL override");
        check(autoPal->consoleProfile() == Common::ConsoleProfile::PAL &&
                  forcedNtsc->consoleProfile() == Common::ConsoleProfile::NTSC &&
                  forcedPal->consoleProfile() == Common::ConsoleProfile::PAL && !board->consoleProfile(),
              "Only supported boards expose their effective console profile");
        autoPal->runCycles(autoPal->cyclesPerFrame());
        check(autoPal->videoFrame().number == 1, "PAL frame executes at the PAL rate");
        check(sms->ram().size() == 8192 && sms->readMemory(0) == 0x31 && sms->readMemory(9) == 0xff, "SMS initial ROM and RAM");
        sms->writeMemory(0, 0xff);
        check(sms->readMemory(0) == 0x31, "SMS ROM must be read-only");
        check(sms->runCycles(34) == 34 && sms->cpu().state().halted && sms->readMemory(0xe005) == 0x42,
              "SMS Z80 program writes mirrored RAM");
        for (const auto type : {SystemType::SG1000, SystemType::MasterSystem})
        {
            const auto &path = type == SystemType::SG1000 ? cpuProgram : smsProgram;
            auto normal = Systems::createBoard(type, Core::Cartridge(path));
            auto accelerated = Systems::createBoard(type, Core::Cartridge(path));
            accelerated->setCPUOverclock(true, 2.0f);
            normal->runCycles(normal->cyclesPerFrame());
            accelerated->runCycles(accelerated->cyclesPerFrame());
            check(accelerated->cpu().state().cycles == normal->cpu().state().cycles * 2, "CPU overclock ratio");
            check(normal->videoFrame().number == 1 && accelerated->videoFrame().number == 1, "Overclock must preserve video cadence");
            std::vector<float> normalAudio, acceleratedAudio;
            normal->takeAudio(normalAudio);
            accelerated->takeAudio(acceleratedAudio);
            check(!normalAudio.empty() && normalAudio.size() == acceleratedAudio.size(), "Overclock must preserve audio cadence");
            accelerated = Systems::createBoard(type, Core::Cartridge(path));
            accelerated->setCPUOverclock(true, 1.37f);
            const auto before = accelerated->cpu().state().cycles;
            for (int i = 0; i < 100; ++i)
                accelerated->runCycles(1);
            check(accelerated->cpu().state().cycles - before == 137, "Fractional CPU overclock over split budgets");
            check(accelerated->runCycles(0) == 0, "Zero overclock budget");
            if (type == SystemType::SG1000)
            {
                const auto saved = accelerated->saveState();
                auto restored = Systems::createBoard(type, Core::Cartridge(path));
                restored->setCPUOverclock(true, 1.37f);
                restored->loadState(saved);
                accelerated->runCycles(1234);
                restored->runCycles(1234);
                check(accelerated->saveState() == restored->saveState(), "Overclock phase must survive state restore");
            }
            accelerated->setCPUOverclock(false, 2.0f);
            const auto disabled = accelerated->cpu().state().cycles;
            accelerated->runCycles(100);
            check(accelerated->cpu().state().cycles == disabled + 100, "Disabled overclock uses normal CPU speed");
        }
        // Como Shadow Dancer, este programa chama uma rotina antes de definir SP.
        // A pilha inicial em FFFF mudaria o banco 0 para 3 ao empilhar o retorno.
        std::vector<std::uint8_t> earlyCallROM(4 * 0x4000, 0);
        const std::uint8_t earlyCallCode[] = {0xcd, 0x10, 0x00, 0xc3, 0x00, 0x04};
        std::copy(std::begin(earlyCallCode), std::end(earlyCallCode), earlyCallROM.begin());
        earlyCallROM[0x10] = 0xc9;
        earlyCallROM[0x400] = 0x76;
        earlyCallROM[0xc400] = 0xc3;
        earlyCallROM[0xc402] = 0x04;
        auto earlyCallCPU = Systems::createBoard(SystemType::MasterSystem,
                                                 Core::Cartridge(fixtures.write("early-call.sms", earlyCallROM)));
        earlyCallCPU->runCycles(100);
        check(earlyCallCPU->cpu().state().halted && earlyCallCPU->cpu().state().pc == 0x401 &&
                  earlyCallCPU->cpu().state().sp == 0xdff0 && earlyCallCPU->readMemory(0x400) == 0x76 &&
                  earlyCallCPU->readMemory(0xdfee) == 3 && earlyCallCPU->readMemory(0xdfef) == 0,
              "SMS direct boot must supply a BIOS-compatible stack without corrupting mapper banks");
        const auto vdpProgram = fixtures.write("vdp-cpu.sms", {0x3e, 0x23, 0xd3, 0xbf, 0x3e, 0x41, 0xd3, 0xbf,
                                                               0x3e, 0x5a, 0xd3, 0xbe, 0x76});
        auto vdpCPU = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(vdpProgram));
        vdpCPU->runCycles(100);
        vdpCPU->output(0xbf, 0x23);
        vdpCPU->output(0x18f, 0x01);
        check(vdpCPU->cpu().state().halted && vdpCPU->input(0x1be) == 0x5a && vdpCPU->input(0x40) == 0 && vdpCPU->input(0x41) == 0xff,
              "SMS Z80 VDP writes, port mirroring and buffered read");
        vdpCPU->output(0xbf, 0x20);
        vdpCPU->output(0xbf, 0x81);
        vdpCPU->runCycles(vdpCPU->cyclesPerFrame());
        check(vdpCPU->videoFrame().number == 1 && (vdpCPU->input(0xbf) & 0x80), "SMS CPU clocks VDP through HALT to VBlank");
        check(!(vdpCPU->input(0xbf) & 0x80), "SMS VDP status read acknowledges VBlank");
        // Interrupção de linha real: handler reconhece BF, altera R8 e registra sua execução na RAM.
        std::vector<std::uint8_t> rasterProgram(0x80, 0);
        const std::vector<std::uint8_t> rasterMain = {0x31, 0x00, 0xdf, 0xed, 0x56, 0xfb, 0x76, 0xc3, 0x06, 0x00};
        const std::vector<std::uint8_t> rasterISR = {0xdb, 0xbf, 0x3e, 0x08, 0xd3, 0xbf, 0x3e, 0x88, 0xd3, 0xbf,
                                                     0x3e, 0x5a, 0x32, 0x00, 0xc0, 0xfb, 0xed, 0x4d};
        std::copy(rasterMain.begin(), rasterMain.end(), rasterProgram.begin());
        std::copy(rasterISR.begin(), rasterISR.end(), rasterProgram.begin() + 0x38);
        auto rasterCPU = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(fixtures.write("raster-cpu.sms", rasterProgram)));
        rasterCPU->output(0xbf, 1);
        rasterCPU->output(0xbf, 0x8a);
        rasterCPU->output(0xbf, 0x14);
        rasterCPU->output(0xbf, 0x80);
        rasterCPU->runCycles(4000);
        check(rasterCPU->readMemory(0xc000) == 0x5a && static_cast<const Core::MasterSystem &>(*rasterCPU).vdp().state().registers[8] == 8,
              "SMS Z80 must service the VDP line interrupt and update scroll");
        // ROM sintética espera a linha B0 via IN (7E), como fazem programas de inicialização.
        const std::vector<std::uint8_t> counterProgram = {0xdb, 0x7e, 0xfe, 0xb0, 0x20, 0xfa, 0x3e, 0x5a, 0x32, 0x00, 0xc0, 0x76};
        auto counterCPU = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(fixtures.write("vcounter.sms", counterProgram)));
        counterCPU->runCycles(1000);
        check(!counterCPU->cpu().state().halted && counterCPU->readMemory(0xc000) == 0, "CPU must wait for vertical line B0");
        counterCPU->runCycles(counterCPU->cyclesPerFrame());
        check(counterCPU->cpu().state().halted && counterCPU->readMemory(0xc000) == 0x5a,
              "CPU must leave its vertical counter polling loop");
        check(counterCPU->input(0x146) == static_cast<const Core::MasterSystem &>(*counterCPU).vdp().verticalCounter(),
              "Even vertical-counter port mirrors ignore upper address bits");
        sms->writeMemory(0xffff, 0x5a);
        check(sms->readMemory(0xdfff) == 0x5a, "SMS upper RAM mirror");
        std::vector<std::uint8_t> smsBanks(8 * 0x4000);
        for (unsigned bank = 0; bank < 8; ++bank)
            std::fill_n(smsBanks.begin() + bank * 0x4000, 0x4000, static_cast<std::uint8_t>(bank));
        auto mapped = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(fixtures.write("banked.sms", smsBanks)));
        check(mapped->readMemory(0x3fff) == 0 && mapped->readMemory(0x4000) == 1 && mapped->readMemory(0xbfff) == 2,
              "SMS initial banks");
        mapped->writeMemory(0xfffd, 3);
        mapped->writeMemory(0xfffe, 4);
        mapped->writeMemory(0xffff, 0xff);
        check(mapped->readMemory(0x03ff) == 0 && mapped->readMemory(0x0400) == 3 && mapped->readMemory(0x3fff) == 3 &&
                  mapped->readMemory(0x4000) == 4 && mapped->readMemory(0x8000) == 7,
              "SMS banks, fixed first KiB and address-line mirroring");
        check(mapped->readMemory(0xdffd) == 3 && mapped->readMemory(0xfffe) == 4, "Mapper writes also update system RAM");
        mapped->writeMemory(0xdffd, 6);
        check(mapped->readMemory(0xfffd) == 6 && mapped->readMemory(0x0400) == 3, "RAM mirror must not change mapper");
        mapped->writeMemory(0x8000, 0x55);
        check(mapped->readMemory(0x8000) == 7, "SMS mapped ROM must be read-only");
        mapped->writeMemory(0xfffc, 0x08);
        mapped->writeMemory(0x8000, 0x11);
        mapped->writeMemory(0xbfff, 0x22);
        mapped->writeMemory(0xffff, 5);
        mapped->writeMemory(0xfffc, 0x0c);
        check(mapped->readMemory(0x8000) == 0, "SMS cartridge RAM banks must be separate");
        mapped->writeMemory(0x8000, 0x33);
        mapped->writeMemory(0xfffc, 0x08);
        check(mapped->readMemory(0x8000) == 0x11 && mapped->readMemory(0xbfff) == 0x22, "SMS cartridge RAM retention");
        mapped->writeMemory(0xfffc, 0);
        check(mapped->readMemory(0x8000) == 5, "SMS ROM bank restored after disabling cartridge RAM");
        mapped->writeMemory(0xfffc, 0x0c);
        check(mapped->readMemory(0x8000) == 0x33, "SMS second cartridge RAM bank retention");
        std::vector<std::uint8_t> bankCode(4 * 0x4000, 0);
        const std::vector<std::uint8_t> switchCode = {0x3e, 0x03, 0x32, 0xff, 0xff, 0xc3, 0x00, 0x80};
        std::copy(switchCode.begin(), switchCode.end(), bankCode.begin());
        bankCode[3 * 0x4000] = 0x76;
        auto bankCPU = Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(fixtures.write("bank-cpu.sms", bankCode)));
        bankCPU->runCycles(60);
        check(bankCPU->cpu().state().halted && bankCPU->cpu().state().pc == 0x8001,
              "SMS CPU must execute code from its newly selected ROM bank");
        const auto largeSms = fixtures.write("too-large.sms", std::vector<std::uint8_t>(0x100001, 0));
        rejects([&]
                { Systems::createBoard(SystemType::MasterSystem, Core::Cartridge(largeSms)); }, "Oversized Sega mapper ROM accepted");
        // A CPU programa a CRAM por OUT e o VDP continua avan�ando durante HALT.
        const std::vector<std::uint8_t> ggVideoCode = {
            0x3e, 0x20, 0xd3, 0xbf, 0x3e, 0xc0, 0xd3, 0xbf,
            0x3e, 0x21, 0xd3, 0xbe, 0x3e, 0x03, 0xd3, 0xbe, 0x76};
        auto ggVideo = Systems::createBoard(SystemType::GameGear, Core::Cartridge(fixtures.write("video.gg", ggVideoCode)));
        ggVideo->runCycles(ggVideo->cyclesPerFrame());
        const auto ggFrame = ggVideo->videoFrame();
        check(ggVideo->cpu().state().halted && ggFrame.number == 1 && ggFrame.width == 160 && ggFrame.height == 144 &&
                  ggFrame.rgba[0] == 17 && ggFrame.rgba[1] == 34 && ggFrame.rgba[2] == 51,
              "Game Gear CPU must program its VDP and publish the cropped frame");
        check((ggVideo->input(0xbf) & 0x80) && !(ggVideo->input(0xbf) & 0x80),
              "Game Gear VBlank status must acknowledge through the VDP port");
        auto gg = Systems::createBoard(SystemType::GameGear, Core::Cartridge(fixtures.write("banked.gg", smsBanks)));
        check(gg->hasMemoryMap() && gg->ram().size() == 8192, "Game Gear must expose its 8 KiB memory map");
        check(gg->clockFrequency() == 3579545 && gg->cyclesPerFrame() == 59736, "Game Gear NTSC timing");
        check(gg->readMemory(0x03ff) == 0 && gg->readMemory(0x4000) == 1 && gg->readMemory(0x8000) == 2,
              "Game Gear initial ROM banks");
        gg->writeMemory(0xfffd, 3);
        gg->writeMemory(0xfffe, 4);
        gg->writeMemory(0xffff, 0xff);
        check(gg->readMemory(0x03ff) == 0 && gg->readMemory(0x0400) == 3 && gg->readMemory(0x4000) == 4 &&
                  gg->readMemory(0x8000) == 7,
              "Game Gear bank selection and fixed ROM");
        gg->writeMemory(0xdffd, 6);
        check(gg->readMemory(0xfffd) == 6 && gg->readMemory(0x0400) == 3, "Game Gear RAM mirror must not change banks");
        gg->writeMemory(0x8000, 0x55);
        check(gg->readMemory(0x8000) == 7, "Game Gear ROM must be read-only");
        gg->writeMemory(0xfffc, 8);
        gg->writeMemory(0x8000, 0x11);
        gg->writeMemory(0xfffc, 12);
        gg->writeMemory(0x8000, 0x22);
        gg->writeMemory(0xfffc, 8);
        check(gg->readMemory(0x8000) == 0x11, "Game Gear cartridge RAM banks must retain data");
        gg->writeMemory(0xfffc, 12);
        check(gg->readMemory(0x8000) == 0x22, "Game Gear second cartridge RAM bank");
        gg->writeMemory(0xfffc, 0);
        check(gg->readMemory(0x8000) == 7, "Game Gear must return to ROM after disabling RAM");
        auto ggCPU = Systems::createBoard(SystemType::GameGear, Core::Cartridge(fixtures.write("bank-cpu.gg", bankCode)));
        ggCPU->runCycles(60);
        check(ggCPU->cpu().state().halted && ggCPU->cpu().state().pc == 0x8001,
              "Game Gear CPU must execute code from its newly selected bank");
        const std::vector<std::uint8_t> ggMemoryCode = {0x3e, 0x5a, 0x32, 0x34, 0xc2, 0x3a, 0x34, 0xe2, 0x76};
        auto ggMemoryCPU = Systems::createBoard(SystemType::GameGear, Core::Cartridge(fixtures.write("memory-cpu.gg", ggMemoryCode)));
        ggMemoryCPU->runCycles(60);
        check(ggMemoryCPU->cpu().state().halted && ggMemoryCPU->cpu().state().a == 0x5a &&
                  ggMemoryCPU->readMemory(0xc234) == 0x5a,
              "Game Gear CPU must write RAM and read its mirror");
        rejects([&]
                { Systems::createBoard(SystemType::GameGear, Core::Cartridge(largeSms)); },
                "Oversized Game Gear ROM accepted");
        for (auto type : {SystemType::SC3000})
        {
            auto other = Systems::createBoard(type, Core::Cartridge(small));
            check(other->type() == type && other->cartridge().rom().size() == 3, "System factory lost cartridge/type");
            check(other->ram().size() == (type == SystemType::SC3000 ? 1024u : 8192u), "System RAM allocation");
            check(!other->hasMemoryMap(), "Unimplemented mapping advertised as available");
            check(!other->clockFrequency(), "Unimplemented system must not be scheduled");
            check(!other->consoleProfile(), "Unimplemented systems must not offer a console profile");
            rejects([&]
                    { other->runCycles(4); }, "CPU executed without an implemented memory map");
            rejects([&]
                    { other->readMemory(0); }, "Unimplemented read silently accepted");
            rejects([&]
                    { other->writeMemory(0, 1); }, "Unimplemented write silently accepted");
        }
        for (const std::size_t offset : {0x1ff0u, 0x3ff0u, 0x7ff0u})
        {
            std::vector<std::uint8_t> headerBytes(offset + 16, 0);
            const std::string signature = "TMR SEGA";
            std::copy(signature.begin(), signature.end(), headerBytes.begin() + offset);
            for (const auto region : {3, 4, 5, 6, 7, 0})
            {
                headerBytes[offset + 15] = static_cast<std::uint8_t>((region << 4) | 0x0c);
                Core::Cartridge cart(fixtures.write("header-" + std::to_string(offset) + "-" + std::to_string(region) + ".sms", headerBytes));
                check(cart.header() && cart.header()->offset == offset && cart.header()->sizeCode == 12, "Header parsing");
                check(cart.suggestedSystem() == (region >= 5 ? SystemType::GameGear : SystemType::MasterSystem), "Header system hint");
                check(cart.conflictingHints() == (region >= 5), "Conflicting hints were hidden");
                check(cart.rom() == headerBytes, "Header parsing modified ROM data");
            }
        }
        for (const auto &pair : {std::make_pair("test.sc", SystemType::SC3000), std::make_pair("test.GG", SystemType::GameGear)})
            check(Core::Cartridge(fixtures.write(pair.first, {0x42})).suggestedSystem() == pair.second, "Extension hint");
        const auto unicode = fixtures.write(u8"ação teste.sg", {1, 2, 3});
        check(Common::readRomFile(unicode) == std::vector<std::uint8_t>({1, 2, 3}), "Unicode path loading");
        rejects([&]
                { Common::readRomFile(fixtures.directory / "missing.sg"); }, "Missing file accepted");
        rejects([&]
                { Common::readRomFile(fixtures.directory); }, "Directory accepted");
        const auto empty = fixtures.write("empty.sg", {});
        rejects([&]
                { Common::readRomFile(empty); }, "Empty ROM accepted");
        const auto zip = fixtures.write("archive.bin", {'P', 'K', 3, 4, 0});
        rejects([&]
                { Common::readRomFile(zip); }, "ZIP accepted as raw ROM");
        const auto oversized = fixtures.write("oversized.bin", std::vector<std::uint8_t>(Common::MaxRomFileSize + 1, 0));
        rejects([&]
                { Common::readRomFile(oversized); }, "Unbounded ROM allocation");
        std::cout << "ROM loading, hints, system creation, SG mapping and failure preservation passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}