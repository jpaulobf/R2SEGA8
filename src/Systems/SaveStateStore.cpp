#include "SaveStateStore.h"
#include "Common/Hash.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace R2SEGA8::Systems
{
    namespace
    {
        // Cria sem seguir um arquivo temporário preexistente. Uma colisão apenas
        // solicita outro nome ao chamador; outras falhas são propagadas.
        bool writeExclusive(const std::filesystem::path &file, const std::vector<std::uint8_t> &data)
        {
#ifdef _WIN32
            HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                const auto error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                    return false;
                throw std::system_error(static_cast<int>(error), std::system_category(), "Unable to create save state temporary file");
            }
            std::size_t offset = 0;
            while (offset < data.size())
            {
                const DWORD count = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 0x7fffffffu));
                DWORD written = 0;
                const BOOL complete = WriteFile(handle, data.data() + offset, count, &written, nullptr);
                if (!complete || written == 0)
                {
                    const auto error = complete ? ERROR_WRITE_FAULT : GetLastError();
                    CloseHandle(handle);
                    std::error_code ignored;
                    std::filesystem::remove(file, ignored);
                    throw std::system_error(static_cast<int>(error), std::system_category(), "Unable to write save state");
                }
                offset += written;
            }
            if (!CloseHandle(handle))
            {
                const auto error = GetLastError();
                std::error_code ignored;
                std::filesystem::remove(file, ignored);
                throw std::system_error(static_cast<int>(error), std::system_category(), "Unable to close save state");
            }
#else
            int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
            flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
            flags |= O_NOFOLLOW;
#endif
            const int descriptor = open(file.c_str(), flags, 0600);
            if (descriptor < 0)
            {
                if (errno == EEXIST)
                    return false;
                throw std::system_error(errno, std::generic_category(), "Unable to create save state temporary file");
            }
            std::size_t offset = 0;
            while (offset < data.size())
            {
                const auto written = write(descriptor, data.data() + offset, data.size() - offset);
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0)
                {
                    const int error = written == 0 ? EIO : errno;
                    close(descriptor);
                    std::error_code ignored;
                    std::filesystem::remove(file, ignored);
                    throw std::system_error(error, std::generic_category(), "Unable to write save state");
                }
                offset += static_cast<std::size_t>(written);
            }
            if (close(descriptor) != 0)
            {
                const int error = errno;
                std::error_code ignored;
                std::filesystem::remove(file, ignored);
                throw std::system_error(error, std::generic_category(), "Unable to close save state");
            }
#endif
            return true;
        }
    }

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
        std::random_device random;
        for (unsigned attempt = 0; attempt < 10; ++attempt)
        {
            const std::uint64_t suffix = (static_cast<std::uint64_t>(random()) << 32) | random();
            std::ostringstream name;
            name << '.' << std::hex << std::setw(16) << std::setfill('0') << suffix << ".tmp";
            auto temporary = destination;
            temporary += name.str();
            if (!writeExclusive(temporary, data))
                continue;
            if (SDL_RenamePath(temporary.u8string().c_str(), destination.u8string().c_str()))
                return;
            const std::string error = SDL_GetError();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("Unable to replace save state: " + error);
        }
        throw std::runtime_error("Unable to create a unique save state temporary file.");
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