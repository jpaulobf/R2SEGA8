#include "KeyboardConfig.h"
#include "Systems/AtomicFile.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    R2Keyboard::Mapping KeyboardConfig::load()
    {
        // Valores ausentes ou inválidos conservam o padrão de cada posição.
        auto mapping = R2Keyboard::defaults();
        canSave_ = true;
        std::ifstream file(path_, std::ios::binary | std::ios::ate);
        if (!file)
        {
            std::error_code error;
            canSave_ = !std::filesystem::exists(path_, error) && !error;
            return mapping;
        }
        // Limita a leitura e impede sobrescrever um arquivo grande ou ilegível.
        const auto size = file.tellg();
        if (size < 0 || size > 4096)
        {
            canSave_ = false;
            return mapping;
        }
        file.seekg(0);
        std::string value;
        for (int p = 0; p < 2; ++p)
            for (int b = 0; b < 10 && std::getline(file, value); ++b)
            {
                if (!value.empty() && value.back() == '\r')
                    value.pop_back();
                unsigned parsed = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (!value.empty() && result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
                    (parsed == 0 || R2Keyboard::assignable(parsed)))
                    mapping[p][b] = parsed;
            }
        if (file.bad())
        {
            canSave_ = false;
            return R2Keyboard::defaults();
        }
        return mapping;
    }
    bool KeyboardConfig::save(const R2Keyboard::Mapping &mapping) const
    {
        if (!canSave_)
            return false;
        std::ostringstream output;
        for (const auto &pad : mapping)
            for (auto key : pad)
                output << key << '\n';
        try
        {
            const auto text = output.str();
            // Substitui o destino somente após gravar um arquivo temporário completo.
            Systems::writeAtomicFile(path_, std::vector<std::uint8_t>(text.begin(), text.end()));
            return true;
        }
        catch (const std::exception &error)
        {
            std::cerr << "KeyboardConfig: Unable to save configuration: " << error.what() << '\n';
            return false;
        }
    }
}