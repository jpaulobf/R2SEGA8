#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace R2SEGA8::Core
{
    // Incremente a versão do estado salvo da Board sempre que os campos ou sua ordem mudarem.
    // Formato explícito little-endian: nunca grava padding, ponteiros ou callbacks.
    class StateArchive
    {
    public:
        explicit StateArchive(std::vector<std::uint8_t> &output) : output_(&output) {}
        explicit StateArchive(const std::vector<std::uint8_t> &input) : input_(&input), inputSize_(input.size()) {}
        StateArchive(const std::vector<std::uint8_t> &input, std::size_t size) : input_(&input), inputSize_(size)
        {
            if (size > input.size())
                throw std::runtime_error("Truncated save state.");
        }
        bool reading() const { return input_ != nullptr; }
        void finish() const
        {
            if (input_ && position_ != inputSize_)

                throw std::runtime_error("Unexpected data in save state.");
        }

        template <class... T>
        void operator()(T &...values) { (value(values), ...); }

    private:
        std::vector<std::uint8_t> *output_ = nullptr;
        const std::vector<std::uint8_t> *input_ = nullptr;
        std::size_t position_ = 0;
        std::size_t inputSize_ = 0;

        // Blocos de bytes já estão no formato do arquivo e não precisam de conversão individual.
        void bytes(std::uint8_t *data, std::size_t size)
        {
            if (reading())
            {
                if (size > inputSize_ - position_)
                    throw std::runtime_error("Truncated save state.");
                if (size)
                    std::memcpy(data, input_->data() + position_, size);
                position_ += size;
            }
            else if (size)
                output_->insert(output_->end(), data, data + size);
        }

        template <class T>
        void value(T &item)
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                std::uint8_t byte = item ? 1 : 0;
                value(byte);
                if (byte > 1)
                    throw std::runtime_error("Invalid save state boolean.");
                item = byte != 0;
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                static_assert(sizeof(T) == 8, "Only binary64 is used in state files");
                std::uint64_t bits = 0;
                if (!reading())
                    std::memcpy(&bits, &item, sizeof(bits));
                value(bits);
                if (reading())
                    std::memcpy(&item, &bits, sizeof(bits));
            }
            else if constexpr (std::is_enum_v<T>)
            {
                std::uint32_t number = static_cast<std::uint32_t>(item);
                value(number);
                item = static_cast<T>(number);
            }
            else if constexpr (std::is_integral_v<T>)
            {
                using U = std::make_unsigned_t<T>;

                // unsigned/int são codificados em 32 bits, independentemente do ABI.
                constexpr unsigned width = sizeof(T) <= 2 ? sizeof(T) : (sizeof(T) <= 4 ? 4 : 8);

                U number = static_cast<U>(item);
                if (reading())
                    number = 0;

                for (unsigned i = 0; i < width; ++i)
                {
                    if (reading())
                    {
                        if (position_ == inputSize_)
                            throw std::runtime_error("Truncated save state.");

                        number |= static_cast<U>((*input_)[position_++]) << (i * 8);
                    }
                    else
                        output_->push_back(static_cast<std::uint8_t>(number >> (i * 8)));
                }
                item = static_cast<T>(number);
            }
            else
                item.state(*this);
        }

        template <class T, std::size_t N>
        void value(std::array<T, N> &items)
        {
            if constexpr (std::is_same_v<T, std::uint8_t>)
                bytes(items.data(), items.size());
            else
                for (auto &item : items)
                    value(item);
        }

        // O tamanho da RAM é determinado pela ROM e não pode ser alterado pelo arquivo.
        template <class T>
        void value(std::vector<T> &items)
        {
            if constexpr (std::is_same_v<T, std::uint8_t>)
                bytes(items.data(), items.size());
            else
                for (auto &item : items)
                    value(item);
        }

        void value(std::string &text)
        {
            std::uint32_t size = static_cast<std::uint32_t>(text.size());
            value(size);
            if (size > 1024 * 1024)
                throw std::runtime_error("Save state string is too large.");

            if (reading())
            {
                if (size > inputSize_ - position_)
                    throw std::runtime_error("Truncated save state string.");
                text.resize(size);
            }

            for (auto &character : text)
                value(character);
        }
    };
}