#include "SN76489.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace R2SEGA8::Core
{
    SN76489::SN76489(unsigned clockFrequency, unsigned sampleRate) : clockFrequency_(clockFrequency), sampleRate_(sampleRate)
    {
        if (!sampleRate || sampleRate > clockFrequency)
            throw std::invalid_argument("Invalid PSG clock/sample rate.");
        for (unsigned i = 0; i < 15; ++i)
            volume_[i] = static_cast<float>(0.25 * std::pow(10.0, -static_cast<double>(i) / 10.0));
        volume_[15] = 0;
        highPass_ = std::exp(-2.0 * 3.14159265358979323846 * 20.0 / sampleRate_);
        samples_.reserve(std::max(1u, sampleRate_ / 4));
        reset();
    }

    void SN76489::reset()
    {
        state_ = {};
        samplePhase_ = 0;
        sampleArea_ = previousInput_ = previousOutput_ = 0;
        samples_.clear();
        updateLevel();
    }

    SN76489::Snapshot SN76489::snapshot() const
    {
        return {state_, clockFrequency_, sampleRate_, samplePhase_, sampleArea_, previousInput_, previousOutput_};
    }

    void SN76489::restore(const Snapshot &saved)
    {
        const auto &s = saved.state;
        if (saved.clockFrequency != clockFrequency_ || saved.sampleRate != sampleRate_ || saved.samplePhase >= clockFrequency_ ||
            !std::isfinite(saved.sampleArea) || !std::isfinite(saved.previousInput) || !std::isfinite(saved.previousOutput) ||
            s.noise > 7 || s.latch > 7 || s.divider >= 16 || s.busyClocks > 32 || s.noiseShift > 0x1ffff ||
            std::any_of(s.tone.begin(), s.tone.end(), [](auto v)
                        { return v > 1023; }) ||
            std::any_of(s.attenuation.begin(), s.attenuation.end(), [](auto v)
                        { return v > 15; }) ||
            std::any_of(s.counter.begin(), s.counter.end(), [](auto v)
                        { return v > 2048; }))
            throw std::invalid_argument("Invalid SN76489 snapshot.");
        state_ = s;
        samplePhase_ = saved.samplePhase;
        sampleArea_ = saved.sampleArea;
        previousInput_ = saved.previousInput;
        previousOutput_ = saved.previousOutput;
        samples_.clear();
        updateLevel();
    }

    void SN76489::write(std::uint8_t value)
    {
        const bool latch = (value & 0x80) != 0;
        if (latch)
            state_.latch = (value >> 4) & 7;
        const auto channel = state_.latch >> 1;
        if (state_.latch & 1)
            state_.attenuation[channel] = value & 15;
        else if (channel < 3)
        {
            auto &tone = state_.tone[channel];
            tone = static_cast<std::uint16_t>(latch ? ((tone & 0x3f0) | (value & 15)) : ((tone & 15) | ((value & 0x3f) << 4)));
        }
        else
        {
            state_.noise = value & 7;
            state_.noiseShift = 0x10000;
            // A escrita reinicia o LFSR, mas não o contador nem o bit já apresentado na saída.
        }
        state_.busyClocks = 32;
        updateLevel();
    }

    unsigned SN76489::tonePeriod(unsigned channel) const
    {
        return state_.tone[channel] ? state_.tone[channel] : 1024;
    }

    unsigned SN76489::noisePeriod() const
    {
        const unsigned rate = state_.noise & 3;
        return rate == 3 ? 2 * tonePeriod(2) : (32u << rate);
    }

    void SN76489::clockDivided()
    {
        bool changed = false;
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            auto &count = state_.counter[channel];
            if (count > 0)
                --count;
            if (!count)
            {
                state_.output[channel] = !state_.output[channel];
                changed = true;
                count = tonePeriod(channel);
            }
        }
        auto &count = state_.counter[3];
        if (count > 0)
            --count;
        if (!count)
        {
            // AN: realimentação de 15 bits, com dois estágios até a saída; não usar os taps do PSG Sega.
            const auto feedback = ((state_.noiseShift >> 2) ^ ((state_.noise & 4) ? (state_.noiseShift >> 3) : 0)) & 1;
            state_.noiseShift = (state_.noiseShift >> 1) | (feedback << 16);
            state_.output[3] = (state_.noiseShift & 1) != 0;
            changed = true;
            count = noisePeriod();
        }
        if (changed)
            updateLevel();
    }

    void SN76489::updateLevel()
    {
        level_ = 0;
        for (unsigned channel = 0; channel < 4; ++channel)
            if (state_.output[channel])
                level_ += volume_[state_.attenuation[channel]] * channelGains_[channel];
        levelPerClock_ = static_cast<double>(level_) * sampleRate_;
    }

    void SN76489::setAudioSettings(const Common::AudioSettings &settings)
    {
        audioSettings_ = settings;
        masterGain_ = settings.master.gain();
        for (unsigned channel = 0; channel < 4; ++channel)
            channelGains_[channel] = settings.channels[channel].gain();
        updateLevel();
    }

    void SN76489::sampleClock()
    {
        // sampleRate_ <= clockFrequency_: cada clock cruza no máximo uma fronteira de amostra.
        // Integra o intervalo inteiro e transfere a área excedente para a janela seguinte.
        const auto phase = static_cast<std::uint64_t>(samplePhase_) + sampleRate_;
        sampleArea_ += levelPerClock_;
        if (phase < clockFrequency_)
        {
            samplePhase_ = static_cast<unsigned>(phase);
            return;
        }
        const auto overflow = static_cast<unsigned>(phase - clockFrequency_);
        const double excess = static_cast<double>(level_) * overflow;
        const double input = (sampleArea_ - excess) / clockFrequency_;
        const double output = input - previousInput_ + highPass_ * previousOutput_;
        previousInput_ = input;
        previousOutput_ = output;
        if (samples_.size() < std::max(1u, sampleRate_ / 4))
            // Master após o filtro garante silêncio mesmo quando ainda existe uma cauda de DC.
            samples_.push_back(static_cast<float>(std::clamp(output, -1.0, 1.0)) * masterGain_);
        samplePhase_ = overflow;
        sampleArea_ = excess;
    }

    void SN76489::tick()
    {
        // A saída vale durante este intervalo; a borda ao final pode iniciar o próximo nível.
        sampleClock();
        ++state_.clocks;
        if (state_.busyClocks)
            --state_.busyClocks;
        if (++state_.divider == 16)
        {
            state_.divider = 0;
            clockDivided();
        }
    }

    void SN76489::runClocks(std::uint64_t clocks)
    {
        while (clocks--)
            tick();
    }

    void SN76489::takeSamples(std::vector<float> &destination)
    {
        destination.swap(samples_);
        samples_.clear();
    }
}