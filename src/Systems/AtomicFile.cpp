#include "AtomicFile.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace R2SEGA8::Systems
{
    void writeAtomicFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &data)
    {
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());
        static std::atomic<unsigned long long> sequence{0};
        std::filesystem::path temporary;
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;
#else
        int file = -1;
#endif
        // Reserva um arquivo temporário exclusivo no mesmo diretório do destino.
        for (unsigned attempt = 0; attempt < 128; ++attempt)
        {
            temporary = path;
            const auto nonce = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
            temporary += ".tmp-" + std::to_string(nonce) + "-" + std::to_string(sequence++);
#ifdef _WIN32
            file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
                break;
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Unable to create exclusive temporary file.");
#else
            file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (file >= 0)
                break;
            if (errno != EEXIST)
                throw std::runtime_error("Unable to create exclusive temporary file.");
#endif
        }
#ifdef _WIN32
        if (file == INVALID_HANDLE_VALUE)
#else
        if (file < 0)
#endif
            throw std::runtime_error("Unable to allocate exclusive temporary file.");
        bool ok = true;
        std::size_t offset = 0;
        while (offset < data.size())
        {
#ifdef _WIN32
            DWORD written = 0;
            const DWORD count = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 0x40000000));
            if (!WriteFile(file, data.data() + offset, count, &written, nullptr) || written == 0)
            {
                ok = false;
                break;
            }
#else
            const auto written = ::write(file, data.data() + offset, data.size() - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
            {
                ok = false;
                break;
            }
#endif
            offset += static_cast<std::size_t>(written);
        }
#ifdef _WIN32
        if (!CloseHandle(file))
            ok = false;
#else
        if (::close(file) != 0)
            ok = false;
#endif
        // Apenas uma gravação completa pode substituir o destino existente.
        if (ok)
        {
#ifdef _WIN32
            ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            ok = ::rename(temporary.c_str(), path.c_str()) == 0;
#endif
        }
        if (!ok)
        {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            throw std::runtime_error("Unable to write or replace file: " + path.u8string());
        }
    }
}