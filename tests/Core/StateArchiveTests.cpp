#include "Core/State/StateArchive.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        using R2SEGA8::Core::StateArchive;
        std::array<std::uint8_t, 3> array{1, 2, 3};
        std::vector<std::uint8_t> bytes{4, 5};
        std::uint16_t word = 0x0706;
        std::vector<std::uint8_t> encoded;
        StateArchive writer(encoded);
        writer(array, bytes, word);
        check(encoded == std::vector<std::uint8_t>({1, 2, 3, 4, 5, 6, 7}), "Archive byte order changed");

        encoded.push_back(0xff);
        std::array<std::uint8_t, 3> restored{};
        std::vector<std::uint8_t> restoredBytes(2);
        std::uint16_t restoredWord = 0;
        StateArchive reader(static_cast<const std::vector<std::uint8_t> &>(encoded), 7);
        reader(restored, restoredBytes, restoredWord);
        reader.finish();
        check(restored == array && restoredBytes == bytes && restoredWord == word, "Archive round trip failed");

        for (std::size_t limit = 0; limit < 7; ++limit)
        {
            bool rejected = false;
            try
            {
                StateArchive truncated(static_cast<const std::vector<std::uint8_t> &>(encoded), limit);
                truncated(restored, restoredBytes, restoredWord);
                truncated.finish();
            }
            catch (const std::runtime_error &)
            {
                rejected = true;
            }
            check(rejected, "Archive accepted a truncated block");
        }
        bool rejected = false;
        try
        {
            StateArchive invalid(static_cast<const std::vector<std::uint8_t> &>(encoded), encoded.size() + 1);
        }
        catch (const std::runtime_error &)
        {
            rejected = true;
        }
        check(rejected, "Archive accepted a limit beyond its input");
        std::cout << "State archive blocks and bounds passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}