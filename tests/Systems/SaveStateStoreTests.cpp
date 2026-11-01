#include "Core/Systems/SG1000/SG1000.h"
#include "Systems/SaveStateStore.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                          ("r2sega8-state-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { std::filesystem::create_directories(directory); }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
        std::filesystem::path rom(const char *name, char byte) const
        {
            const auto path = directory / name;
            std::ofstream file(path, std::ios::binary);
            file.put(byte);
            file.close();
            check(static_cast<bool>(file), "Unable to write ROM fixture");
            return path;
        }
    };
}

int main()
{
    try
    {
        using namespace R2SEGA8;
        Fixture fixture;
        Core::SG1000 board{Core::Cartridge(fixture.rom("one.sg", '\x01'))};
        Core::SG1000 other{Core::Cartridge(fixture.rom("two.sg", '\x02'))};
        Systems::SaveStateStore store(fixture.directory / "saves");
        board.writeMemory(0xc000, 0x42);
        const auto first = board.saveState();
        store.save(board, 1);
        check(std::filesystem::exists(fixture.directory / "saves" / "1-af63bc4c8601b62c.slot1.state"),
              "Existing save state file names must remain compatible");
        board.writeMemory(0xc000, 0x77);
        const auto second = board.saveState();
        store.save(board, 2);
        check(store.load(board, 1) == first && store.load(board, 2) == second, "Slots must preserve their own state");
        store.save(board, 1);
        check(store.load(board, 1) == second, "Saving again must replace the previous state");
        bool rejected = false;
        try
        {
            store.load(other, 1);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "A different ROM must not share the slot");
        rejected = false;
        try
        {
            store.save(board, 0);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "Invalid slots must be rejected");
        std::cout << "Save state store tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}