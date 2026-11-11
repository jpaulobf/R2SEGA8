#include "../Support/CartridgeFixture.h"
#include "Common/RomFile.h"
#include "Core/Cartridge/Cartridge.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

using namespace R2SEGA8::Common;
using R2SEGA8::Core::Cartridge;
using Bytes = std::vector<uint8_t>;
static void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
static void put16(Bytes &v, uint16_t n)
{
    v.push_back(uint8_t(n));
    v.push_back(uint8_t(n >> 8));
}
static void put32(Bytes &v, uint32_t n)
{
    put16(v, uint16_t(n));
    put16(v, uint16_t(n >> 16));
}
static void set32(Bytes &v, size_t p, uint32_t n)
{
    for (int i = 0; i < 4; ++i)
        v[p + i] = uint8_t(n >> (8 * i));
}

static Bytes image()
{
    Bytes v(16 + 16384, 0);
    v[0] = 'N';
    v[1] = 'E';
    v[2] = 'S';
    v[3] = 0x1a;
    v[4] = 1;
    return v;
}

static Bytes archive(const Bytes &raw, bool deflated, bool descriptor, const std::string &name = "folder/game.sg")
{
    Bytes payload = raw;
    if (deflated)
    {
        z_stream s{};
        check(deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "deflate init");
        payload.resize(compressBound(raw.size()));
        s.next_in = const_cast<Bytef *>(raw.data());
        s.avail_in = static_cast<uInt>(raw.size());
        s.next_out = payload.data();
        s.avail_out = static_cast<uInt>(payload.size());
        check(deflate(&s, Z_FINISH) == Z_STREAM_END, "deflate");
        payload.resize(s.total_out);
        deflateEnd(&s);
    }
    const uint32_t crc = crc32(0, raw.data(), static_cast<uInt>(raw.size()));
    const uint16_t flags = descriptor ? 8 : 0, method = deflated ? 8 : 0;
    Bytes v;
    put32(v, 0x04034b50);
    put16(v, 20);
    put16(v, flags);
    put16(v, method);
    put32(v, 0);
    put32(v, descriptor ? 0 : crc);
    put32(v, descriptor ? 0 : uint32_t(payload.size()));
    put32(v, descriptor ? 0 : uint32_t(raw.size()));
    put16(v, uint16_t(name.size()));
    put16(v, 0);
    v.insert(v.end(), name.begin(), name.end());
    v.insert(v.end(), payload.begin(), payload.end());
    if (descriptor)
    {
        put32(v, 0x08074b50);
        put32(v, crc);
        put32(v, uint32_t(payload.size()));
        put32(v, uint32_t(raw.size()));
    }
    const uint32_t central = uint32_t(v.size());
    put32(v, 0x02014b50);
    put16(v, 20);
    put16(v, 20);
    put16(v, flags);
    put16(v, method);
    put32(v, 0);
    put32(v, crc);
    put32(v, uint32_t(payload.size()));
    put32(v, uint32_t(raw.size()));
    put16(v, uint16_t(name.size()));
    put16(v, 0);
    put16(v, 0);
    put16(v, 0);
    put16(v, 0);
    put32(v, 0);
    put32(v, 0);
    v.insert(v.end(), name.begin(), name.end());
    const uint32_t centralSize = uint32_t(v.size()) - central;
    put32(v, 0x06054b50);
    put16(v, 0);
    put16(v, 0);
    put16(v, 1);
    put16(v, 1);
    put32(v, centralSize);
    put32(v, central);
    put16(v, 0);
    return v;
}

static void save(const std::filesystem::path &path, const Bytes &v)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(v.data()), v.size());
    check(bool(f), "write fixture");
}

int main(int argc, char **argv)
{
    try
    {
        check(argc == 2, "fixture directory required");
        const std::filesystem::path dir = argv[1];
        std::filesystem::create_directories(dir);
        const Bytes raw = image();
        std::string name;
        for (bool deflated : {false, true})
            for (bool descriptor : {false, true})
            {
                const auto zip = archive(raw, deflated, descriptor, "folder/GAME.SG");
                Bytes out{42};
                check(extractRomFromZip(zip, out, name) && out == raw, "valid ZIP rejected");
                save(dir / "valid.zip", zip);
                auto c = ::R2SEGA8::Tests::loadCartridge(dir / "valid.zip");
                check(c.suggestedSystem() == R2SEGA8::Common::SystemType::SG1000, "ZIP system detection");
            }
        save(dir / "valid.zip", archive(raw, true, false, "game.sg"));
        auto loaded = readRomFile(dir / "valid.zip", &name);
        auto replacement = raw;
        replacement[0] ^= 1;
        save(dir / "valid.zip", archive(replacement, true, false, "game.gg"));
        Cartridge stable(dir / "valid.zip", std::move(loaded), name);
        check(stable.rom() == raw && stable.suggestedSystem() == SystemType::SG1000,
              "Preloaded ROM must remain unchanged when the archive is replaced");
        const Bytes valid = archive(raw, true, false);
        const size_t eocd = valid.size() - 22;
        const size_t cd = uint32_t(valid[eocd + 16]) | (uint32_t(valid[eocd + 17]) << 8) |
                          (uint32_t(valid[eocd + 18]) << 16) | (uint32_t(valid[eocd + 19]) << 24);
        const auto reject = [](const Bytes &v)
        { Bytes out{42}; std::string name = "unchanged"; check(!extractRomFromZip(v, out, name) && out == Bytes{42} && name == "unchanged", "invalid ZIP accepted or output changed"); };
        for (size_t n = 0; n < valid.size(); ++n)
            reject(Bytes(valid.begin(), valid.begin() + n));
        auto bad = valid;
        set32(bad, cd + 24, 0xffffffff);
        reject(bad); // ZIP bomb / ZIP64 size
        bad = valid;
        set32(bad, cd + 20, 0xffffffff);
        reject(bad);
        bad = valid;
        set32(bad, cd + 42, 0xffffffff);
        reject(bad);
        bad = valid;
        set32(bad, eocd + 16, 0xffffffff);
        reject(bad);
        bad = valid;
        bad[cd + 8] |= 1;
        reject(bad); // encryption
        bad = valid;
        bad[cd + 10] = 99;
        reject(bad); // method
        bad = valid;
        bad[cd + 16] ^= 1;
        reject(bad); // CRC mismatch
        bad = valid;
        bad[eocd + 4] = 1;
        reject(bad); // multi-disk
        bad = valid;
        bad[eocd + 10] = 2;
        reject(bad); // mismatched entry counts
        bad = valid;
        set32(bad, eocd + 12, 0xffffffff);
        reject(bad);
        bad = valid;
        bad[30] ^= 1;
        reject(bad); // local/central name disagreement
        bad = valid;
        bad[cd + 28] = 0xff;
        bad[cd + 29] = 0xff;
        reject(bad);
        bad = valid;
        bad[6] |= 1;
        reject(bad); // local/central flags disagree
        bad = archive(raw, false, false);
        bad[30 + std::string("folder/game.sg").size()] ^= 1;
        reject(bad);
        bad = valid;
        bad[30 + std::string("folder/game.sg").size()] ^= 0xff;
        reject(bad);
        reject(archive(raw, false, false, "readme.txt"));

        // Deterministic bounded mutation corpus for sanitizer runs; acceptance
        // is allowed only within the documented output limit.
        uint32_t seed = 0x184ad937;
        const auto random = [&seed]()
        { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        for (int iteration = 0; iteration < 2000; ++iteration)
        {
            auto mutated = valid;
            for (unsigned j = 0, count = 1 + random() % 4; j < count; ++j)
                mutated[random() % mutated.size()] = static_cast<uint8_t>(random());
            Bytes out;
            if (extractRomFromZip(mutated, out, name))
                check(!out.empty() && out.size() <= MaxRomFileSize, "mutation exceeded output limit");
        }

        for (const auto &entry : {std::make_pair("game.sc", SystemType::SC3000),
                                  std::make_pair("game.sms", SystemType::MasterSystem),
                                  std::make_pair("game.gg", SystemType::GameGear)})
        {
            save(dir / "valid.zip", archive(raw, true, false, entry.first));
            check(::R2SEGA8::Tests::loadCartridge(dir / "valid.zip").suggestedSystem() == entry.second, "ZIP system detection");
        }
        std::cout << "ROM and ZIP bounds tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}