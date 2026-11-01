#pragma once

#include "Common/ExecutionSettings.h"
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace R2SEGA8::Systems
{
    // Histórico circular de estados, amostrado e reproduzido na cadência de quadros da máquina.
    class RewindBuffer
    {
    public:
        void configure(unsigned frequency, unsigned cyclesPerFrame, Common::RewindLevel level)
        {
            if (!frequency || !cyclesPerFrame)
                throw std::invalid_argument("Invalid rewind timing.");
            interval_ = level == Common::RewindLevel::Light ? 10 : level == Common::RewindLevel::Precise ? 2
                                                                                                         : 5;
            const auto denominator = static_cast<std::uint64_t>(cyclesPerFrame) * interval_;
            const auto slots = (static_cast<std::uint64_t>(frequency) * 10 + denominator - 1) / denominator + 1;
            states_ = std::vector<std::vector<std::uint8_t>>(static_cast<std::size_t>(slots));
            first_ = count_ = captureCounter_ = rewindCounter_ = 0;
        }

        void clear()
        {
            states_.clear();
            first_ = count_ = captureCounter_ = rewindCounter_ = 0;
        }

        bool configured() const { return !states_.empty(); }

        // Chamada uma vez antes de cada quadro executado; o primeiro estado é capturado imediatamente.
        bool captureDue()
        {
            const bool due = captureCounter_ == 0;
            captureCounter_ = (captureCounter_ + 1) % interval_;
            return due;
        }

        void push(std::vector<std::uint8_t> state)
        {
            if (states_.empty())
                throw std::logic_error("Rewind buffer is not configured.");
            if (count_ == states_.size())
            {
                states_[first_] = std::move(state);
                first_ = (first_ + 1) % states_.size();
            }
            else
            {
                states_[(first_ + count_) % states_.size()] = std::move(state);
                ++count_;
            }
        }

        void beginRewind() { rewindCounter_ = 0; }
        void endRewind() { captureCounter_ = 0; }

        // Retorna um estado por intervalo, consumindo o futuro para permitir uma nova linha do tempo.
        const std::vector<std::uint8_t> *step()
        {
            if (rewindCounter_ != 0)
            {
                --rewindCounter_;
                return nullptr;
            }
            if (count_ == 0)
                return nullptr;
            rewindCounter_ = interval_ - 1;
            --count_;
            return &states_[(first_ + count_) % states_.size()];
        }

    private:
        std::vector<std::vector<std::uint8_t>> states_;
        std::size_t first_ = 0, count_ = 0;
        unsigned interval_ = 1, captureCounter_ = 0, rewindCounter_ = 0;
    };
}