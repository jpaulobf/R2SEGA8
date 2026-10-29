#include "Common/RomFile.h"
#include "Systems/System.h"
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
        auto system = Systems::createSystem(SystemType::SG1000, Core::Cartridge(sgPath));
        check(system->cartridge().rom() == bytes, "ROM bytes changed on load");
        check(system->cartridge().suggestedSystem() == SystemType::SG1000, "Case-insensitive extension hint");
        check(system->ram().size() == 1024, "SG RAM size");
        for (unsigned address = 0; address < 0xc000; ++address)
            check(system->readMemory(static_cast<std::uint16_t>(address)) == bytes[address], "SG ROM mapping");
        for (unsigned i = 0; i < 1024; ++i)
            system->writeMemory(static_cast<std::uint16_t>(0xc000 + i), static_cast<std::uint8_t>(i * 7));
        for (unsigned address = 0xc000; address <= 0xffff; ++address)
            check(system->readMemory(static_cast<std::uint16_t>(address)) == static_cast<std::uint8_t>((address & 1023) * 7),
                  "SG RAM must mirror through FFFF");
        system->writeMemory(0, 0x99);
        check(system->readMemory(0) == bytes[0], "Writes must not change ROM");
        auto *previous = system.get();
        rejects([&]
                { system = Systems::createSystem(SystemType::Unknown, Core::Cartridge(sgPath)); }, "Unknown system accepted");
        check(system.get() == previous, "Failed load replaced the active machine");
        const auto banked = fixtures.write("banked.sg", std::vector<std::uint8_t>(0x10000, 1));
        rejects([&]
                { system = Systems::createSystem(SystemType::SG1000, Core::Cartridge(banked)); }, "Oversized SG plain ROM accepted");
        check(system.get() == previous && system->readMemory(0xc007) == 49, "Failed replacement damaged RAM");

        const auto small = fixtures.write("small.bin", {0x12, 0x34, 0x56});
        auto smallSystem = Systems::createSystem(SystemType::SG1000, Core::Cartridge(small));
        check(smallSystem->readMemory(2) == 0x56 && smallSystem->readMemory(3) == 0xff, "Unpopulated ROM handling");
        check(smallSystem->cartridge().suggestedSystem() == SystemType::Unknown, "Unknown extension guessed a system");
        check(std::all_of(smallSystem->ram().begin(), smallSystem->ram().end(), [](auto value)
                          { return value == 0; }),
              "RAM initialization");
        for (auto type : {SystemType::SC3000, SystemType::MasterSystem, SystemType::GameGear})
        {
            auto other = Systems::createSystem(type, Core::Cartridge(small));
            check(other->type() == type && other->cartridge().rom().size() == 3, "System factory lost cartridge/type");
            check(other->ram().size() == (type == SystemType::SC3000 ? 1024u : 8192u), "System RAM allocation");
            check(!other->hasMemoryMap(), "Unimplemented mapping advertised as available");
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