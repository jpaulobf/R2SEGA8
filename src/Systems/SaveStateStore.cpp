#include "SaveStateStore.h"
#include "Common/Hash.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace R2SEGA8::Systems
{
    SaveStateStore::SaveStateStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

    std::filesystem::path SaveStateStore::path(const Core::Board &board, unsigned slot) const
    {
        if (!board.supportsSaveState() || slot < 1 || slot > 3)
            throw std::runtime_error("Save state unavailable or invalid slot.");
        const auto hash = Common::fnv1a64(board.cartridge().rom());
        std::ostringstream name;
        name << static_cast<unsigned>(board.type()) << '-' << std::hex << std::setw(16) << std::setfill('0') << hash
             << ".slot" << slot << ".state";
        return directory_ / name.str();
    }

    void SaveStateStore::save(const Core::Board &board, unsigned slot) const
    {
        const auto destination = path(board, slot);
        const auto data = board.saveState();
        std::filesystem::create_directories(destination.parent_path());
        auto temporary = destination;
        temporary += ".tmp";
        if (!SDL_SaveFile(temporary.u8string().c_str(), data.data(), data.size()))
            throw std::runtime_error(std::string("Unable to write save state: ") + SDL_GetError());
        if (!SDL_RenamePath(temporary.u8string().c_str(), destination.u8string().c_str()))
            throw std::runtime_error(std::string("Unable to replace save state: ") + SDL_GetError());
    }

    std::vector<std::uint8_t> SaveStateStore::load(const Core::Board &board, unsigned slot) const
    {
        std::ifstream file(path(board, slot), std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("No readable save state in this slot.");
        const auto length = file.tellg();
        if (length <= 0 || length > 1024 * 1024)
            throw std::runtime_error("Invalid save state size.");
        std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(data.data()), length))
            throw std::runtime_error("Unable to read save state.");
        return data;
    }
}