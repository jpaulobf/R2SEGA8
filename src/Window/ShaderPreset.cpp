#include "ShaderPreset.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace R2SEGA8::Window
{
    namespace
    {
        std::string trim(const std::string &s)
        {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        }

        void checkIncludes(const std::filesystem::path &path, std::set<std::filesystem::path> &visited)
        {
            // Resolve cada include em relação ao arquivo que o declara. O conjunto
            // evita visitas repetidas e impede recursão infinita em ciclos.
            const auto resolved = std::filesystem::absolute(path).lexically_normal();
            if (!visited.insert(resolved).second)
                return;
            std::ifstream file(resolved);
            if (!file)
                throw std::runtime_error("Cannot open shader or include: " + resolved.string());
            const std::regex includePattern(R"re(^\s*#\s*include\s*"([^"]+)")re");
            std::string line;
            std::smatch match;
            while (std::getline(file, line))
                if (std::regex_search(line, match, includePattern))
                    checkIncludes(resolved.parent_path() / match[1].str(), visited);
        }

        double positiveNumber(const std::string &value)
        {
            size_t used = 0;
            double result = std::stod(value, &used);
            if (used != value.size() || !std::isfinite(result) || result <= 0)
                throw std::runtime_error("Invalid positive number: " + value);
            return result;
        }

        unsigned nonnegativeInteger(const std::string &value)
        {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("Invalid integer: " + value);
            const auto n = std::stoull(value);
            if (n > std::numeric_limits<unsigned>::max())
                throw std::runtime_error("Integer too large");
            return static_cast<unsigned>(n);
        }

        ShaderScale scaleType(const std::string &value)
        {
            if (value == "source")
                return ShaderScale::Source;
            if (value == "viewport")
                return ShaderScale::Viewport;
            if (value == "absolute")
                return ShaderScale::Absolute;
            throw std::runtime_error("Unsupported scale type: " + value);
        }

        bool boolean(const std::string &value)
        {
            if (value == "true")
                return true;
            if (value == "false")
                return false;
            throw std::runtime_error("Invalid boolean: " + value);
        }
    }

    bool loadShaderPreset(const std::filesystem::path &path, std::vector<ShaderPass> &passes, std::string &error,
                          std::filesystem::path *lookupTexture, std::map<std::string, std::string> *parameters)
    {
        try
        {
            std::ifstream file(path);
            if (!file)
                throw std::runtime_error("Cannot open preset: " + path.string());
            std::map<std::string, std::string> values;
            std::string line;
            // Guarda as chaves antes de interpretá-las para detectar duplicatas
            // e, ao final, rejeitar opções que ainda não são suportadas.
            while (std::getline(file, line))
            {
                line = trim(line);
                if (line.empty() || line[0] == '#')
                    continue;
                const auto separator = line.find('=');
                if (separator == std::string::npos)
                    throw std::runtime_error("Invalid preset line: " + line);
                auto key = trim(line.substr(0, separator));
                auto value = trim(line.substr(separator + 1));
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                if (key.empty() || value.empty() || !values.emplace(key, value).second)
                    throw std::runtime_error("Invalid or duplicate preset key: " + key);
            }
            auto countIt = values.find("shaders");
            if (countIt == values.end())
                throw std::runtime_error("Missing shaders count");
            unsigned count = nonnegativeInteger(countIt->second);
            if (count == 0 || count > 32)
                throw std::runtime_error("Invalid shaders count");
            values.erase(countIt);
            std::map<std::string, std::string> overrides;
            auto parameterList = values.find("parameters");
            if (parameterList != values.end())
            {
                std::string names = parameterList->second;
                values.erase(parameterList);
                size_t start = 0;
                while (start <= names.size())
                {
                    const size_t end = names.find(';', start);
                    const std::string name = trim(names.substr(start, end == std::string::npos ? end : end - start));
                    if (!std::regex_match(name, std::regex(R"([A-Za-z_][A-Za-z_0-9]*)")))
                        throw std::runtime_error("Invalid parameter name: " + name);
                    auto value = values.find(name);
                    if (value == values.end())
                        throw std::runtime_error("Missing parameter value: " + name);
                    size_t used = 0;
                    const double number = std::stod(value->second, &used);
                    if (used != value->second.size() || !std::isfinite(number) || !overrides.emplace(name, value->second).second)
                        throw std::runtime_error("Invalid parameter value: " + name);
                    values.erase(value);
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
            }
            std::filesystem::path lookup;
            auto texturesIt = values.find("textures");
            // O HQ2 usa uma única tabela de consulta (LUT), sem interpolação.
            if (texturesIt != values.end())
            {
                if (texturesIt->second != "LUT")
                    throw std::runtime_error("Unsupported lookup texture list");
                values.erase(texturesIt);
                auto lutIt = values.find("LUT");
                if (lutIt == values.end())
                    throw std::runtime_error("Missing LUT path");
                lookup = (path.parent_path() / lutIt->second).lexically_normal();
                values.erase(lutIt);
                if (!std::filesystem::is_regular_file(lookup))
                    throw std::runtime_error("Cannot open LUT: " + lookup.string());
                auto filter = values.find("LUT_linear");
                if (filter != values.end())
                {
                    if (boolean(filter->second))
                        throw std::runtime_error("Linear LUT filtering is unsupported");
                    values.erase(filter);
                }
            }
            std::vector<ShaderPass> result(count);
            std::set<std::filesystem::path> visited;
            auto take = [&](const std::string &key, auto apply)
            {
                auto it = values.find(key);
                if (it != values.end())
                {
                    apply(it->second);
                    values.erase(it);
                }
            };
            for (unsigned i = 0; i < count; ++i)
            {
                auto &pass = result[i];
                const std::string suffix = std::to_string(i);
                take("shader" + suffix, [&](const std::string &v)
                     { pass.source = (path.parent_path() / v).lexically_normal(); });
                if (pass.source.empty())
                    throw std::runtime_error("Missing shader" + suffix);
                checkIncludes(pass.source, visited);
                // Sem escala explícita, o último passe usa o tamanho da saída.
                if (i + 1 == count)
                    pass.scaleX = pass.scaleY = ShaderScale::Viewport;
                take("filter_linear" + suffix, [&](const std::string &v)
                     { pass.linear = boolean(v); });
                take("float_framebuffer" + suffix, [&](const std::string &v)
                     { pass.floatingPoint = boolean(v); });
                take("srgb_framebuffer" + suffix, [&](const std::string &v)
                     { pass.srgb = boolean(v); });
                if (pass.floatingPoint && pass.srgb)
                    throw std::runtime_error("Floating-point and sRGB framebuffers cannot be combined");
                take("frame_count_mod" + suffix, [&](const std::string &v)
                     { pass.frameCountMod = nonnegativeInteger(v); });
                // Opções por eixo sobrescrevem a opção aplicada aos dois eixos.
                take("scale_type" + suffix, [&](const std::string &v)
                     { pass.scaleX = pass.scaleY = scaleType(v); });
                take("scale_type_x" + suffix, [&](const std::string &v)
                     { pass.scaleX = scaleType(v); });
                take("scale_type_y" + suffix, [&](const std::string &v)
                     { pass.scaleY = scaleType(v); });
                take("scale" + suffix, [&](const std::string &v)
                     { pass.factorX = pass.factorY = positiveNumber(v); });
                take("scale_x" + suffix, [&](const std::string &v)
                     { pass.factorX = positiveNumber(v); });
                take("scale_y" + suffix, [&](const std::string &v)
                     { pass.factorY = positiveNumber(v); });
            }
            if (!values.empty())
                throw std::runtime_error("Unsupported preset setting: " + values.begin()->first);
            // Publica os resultados somente depois de validar todo o preset.
            passes = std::move(result);
            if (lookupTexture)
                *lookupTexture = std::move(lookup);
            if (parameters)
                *parameters = std::move(overrides);
            error.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }

    bool planShaderPasses(const std::vector<ShaderPass> &passes, ShaderPassSize source,
                          ShaderPassSize viewport, std::vector<ShaderPassSize> &sizes, std::string &error)
    {
        try
        {
            if (passes.empty() || source.width <= 0 || source.height <= 0 || viewport.width <= 0 || viewport.height <= 0)
                throw std::runtime_error("Invalid source, viewport or empty pipeline");
            std::vector<ShaderPassSize> result;
            const auto dimension = [](ShaderScale type, double factor, int input, int output)
            {
                const double size = (type == ShaderScale::Absolute ? 1.0 : type == ShaderScale::Source ? input
                                                                                                       : output) *
                                    factor;
                if (!std::isfinite(size) || size < 1 || size > std::numeric_limits<int>::max())
                    throw std::runtime_error("Invalid pass dimensions");
                return static_cast<int>(size);
            };
            for (const auto &pass : passes)
            {
                // A saída calculada torna-se a entrada do próximo passe.
                source = {dimension(pass.scaleX, pass.factorX, source.width, viewport.width),
                          dimension(pass.scaleY, pass.factorY, source.height, viewport.height)};
                result.push_back(source);
            }
            sizes = std::move(result);
            error.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }
}