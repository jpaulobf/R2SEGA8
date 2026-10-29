#include "Common/RomFile.h"
#include "Systems/Board.h"
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
        check(executing->input(0xbe) == 0xff, "Unconnected ports return FF");
        executing->output(0xbe, 0x55);
        executing->cpu().reset();
        check(executing->cpu().state().pc == 0 && executing->readMemory(0xc005) == 0x42, "CPU reset does not clear system RAM");
        check(std::all_of(smallBoard->ram().begin(), smallBoard->ram().end(), [](auto value)
                          { return value == 0; }),
              "RAM initialization");
        for (auto type : {SystemType::SC3000, SystemType::MasterSystem, SystemType::GameGear})
        {
            auto other = Systems::createBoard(type, Core::Cartridge(small));
            check(other->type() == type && other->cartridge().rom().size() == 3, "System factory lost cartridge/type");
            check(other->ram().size() == (type == SystemType::SC3000 ? 1024u : 8192u), "System RAM allocation");
            check(!other->hasMemoryMap(), "Unimplemented mapping advertised as available");
            check(!other->clockFrequency(), "Unimplemented system must not be scheduled");
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