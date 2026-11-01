#include "Core/Audio/SN76489/SN76489.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    using PSG = R2SEGA8::Core::SN76489;

    // Interrompe a validação com o comportamento esperado.
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    // Programa os dez bits do divisor de um dos três canais de tom.
    void tone(PSG &psg, unsigned channel, unsigned period)
    {
        psg.write(static_cast<std::uint8_t>(0x80 | (channel << 5) | (period & 15)));
        psg.write(static_cast<std::uint8_t>((period >> 4) & 63));
    }

    // Silencia os quatro canais sem interromper os respectivos osciladores.
    void mute(PSG &psg)
    {
        for (unsigned channel = 0; channel < 4; ++channel)
            psg.write(static_cast<std::uint8_t>(0x9f | (channel << 5)));
    }

    // Mede o valor RMS no PCM após o transitório do filtro de DC.
    double rms(const std::vector<float> &samples)
    {
        double energy = 0;
        for (std::size_t i = 1000; i < samples.size(); ++i)
            energy += samples[i] * samples[i];
        return std::sqrt(energy / (samples.size() - 1000));
    }
}

int main()
{
    try
    {
        PSG psg;
        check(psg.ready() && psg.state().noiseShift == 0x10000 && psg.state().attenuation[0] == 0, "AN startup state/READY");
        psg.write(0x85);
        psg.write(0x2a);
        check(psg.state().tone[0] == 0x2a5, "Tone latch/data assembly");
        psg.write(0x8c);
        check(psg.state().tone[0] == 0x2ac, "Latch must preserve tone high bits");
        psg.write(0x7f);
        check(psg.state().tone[0] == 0x3fc, "Tone must mask data to six high bits");
        psg.write(0xb3);
        psg.write(0x7a);
        check(psg.state().attenuation[1] == 10 && psg.state().tone[0] == 0x3fc, "Data-only volume write must retain the selected register");
        psg.write(0xef);
        check(psg.state().noise == 7, "Noise uses only three control bits");
        psg.runClocks(31);
        check(!psg.ready(), "READY must remain low for 32 clocks");
        psg.tick();
        check(psg.ready(), "READY must recover after exactly 32 clocks");

        psg.reset();
        for (unsigned channel = 0; channel < 3; ++channel)
            tone(psg, channel, channel + 1);
        psg.runClocks(15);
        check(!psg.state().output[0], "Internal divider must wait for 16 input clocks");
        psg.tick();
        for (unsigned tick = 1; tick <= 25; ++tick)
        {
            if (tick > 1)
                psg.runClocks(16);
            for (unsigned channel = 0; channel < 3; ++channel)
                check(psg.state().output[channel] == ((((tick - 1) / (channel + 1) + 1) & 1) != 0), "Independent tone half periods");
        }
        psg.reset();
        psg.runClocks(16);
        check(psg.state().counter[0] == 1024 && psg.state().output[0], "TI zero divisor is 1024");
        tone(psg, 0, 3);
        check(psg.state().counter[0] == 1024, "Tone write must not reset the running counter");
        psg.runClocks(16 * 1023);
        check(psg.state().output[0], "Existing half period must finish after a write");
        psg.runClocks(16);
        check(!psg.state().output[0] && psg.state().counter[0] == 3, "New divisor applies on counter reload");

        // Oráculo em outra representação: LFSR de 15 bits + dois estágios de atraso de saída.
        for (unsigned mode = 0; mode < 8; ++mode)
        {
            PSG noise;
            tone(noise, 2, 5);
            noise.write(static_cast<std::uint8_t>(0xe0 | mode));
            std::uint16_t lfsr = 0x4000;
            unsigned pipeline = 0;
            const unsigned interval = (mode & 3) == 3 ? 10 : (32u << (mode & 3));
            for (unsigned shift = 0; shift < 400; ++shift)
            {
                noise.runClocks(shift ? 16 * interval : 16);
                pipeline = (pipeline >> 1) | ((lfsr & 1) << 1);
                const auto feedback = (lfsr ^ ((mode & 4) ? (lfsr >> 1) : 0)) & 1;
                lfsr = static_cast<std::uint16_t>((lfsr >> 1) | (feedback << 14));
                check(noise.state().output[3] == ((pipeline & 1) != 0), "AN white/periodic noise sequence and rate");
                check(noise.state().noiseShift == ((static_cast<std::uint32_t>(lfsr) << 2) | pipeline),
                      "Noise feedback must match the AN pipeline, not the Sega PSG");
            }
            const auto count = noise.state().counter[3];
            const auto output = noise.state().output[3];
            noise.write(static_cast<std::uint8_t>(mode));
            check(noise.state().noiseShift == 0x10000 && noise.state().counter[3] == count && noise.state().output[3] == output,
                  "Repeated data-only noise write resets LFSR but preserves counter/output");
        }
        PSG linked;
        tone(linked, 2, 5);
        linked.write(0xe3);
        linked.runClocks(16);
        check(linked.state().counter[3] == 10, "Noise mode 3 follows tone 2 period");
        tone(linked, 2, 0);
        linked.runClocks(16 * 10);
        check(linked.state().counter[3] == 2048, "Noise mode 3 follows changes and TI zero divisor");

        PSG volume;
        mute(volume);
        volume.runClocks(16);
        check(volume.level() == 0 && volume.state().output[0], "Muted oscillators must continue running");
        for (unsigned step = 0; step < 16; ++step)
        {
            volume.write(static_cast<std::uint8_t>(0x90 | step));
            const double expected = step == 15 ? 0 : 0.25 * std::pow(10.0, -static_cast<double>(step) / 10.0);
            check(std::abs(volume.level() - expected) < 1e-7, "2 dB attenuation and exact silence at F");
        }

        // Os mesmos clocks e escritas precisam gerar PCM idêntico, independentemente da coleta por quadro.
        PSG whole, chunks;
        mute(whole);
        mute(chunks);
        tone(whole, 0, 254);
        tone(chunks, 0, 254);
        whole.write(0x90);
        chunks.write(0x90);
        whole.runClocks(350000);
        std::vector<float> expected, collected, part;
        whole.takeSamples(expected);
        for (unsigned elapsed = 0; elapsed < 350000;)
        {
            const auto count = std::min(127u, 350000 - elapsed);
            chunks.runClocks(count);
            chunks.takeSamples(part);
            collected.insert(collected.end(), part.begin(), part.end());
            elapsed += count;
        }
        check(expected == collected && expected.size() == 350000ull * PSG::DefaultSampleRate / PSG::DefaultClock,
              "Rational PCM integration and phase across arbitrarily sized batches");
        unsigned positiveCrossings = 0;
        for (std::size_t i = 1001; i < expected.size(); ++i)
            if (expected[i - 1] <= 0 && expected[i] > 0)
                ++positiveCrossings;
        const double frequency = positiveCrossings * 48000.0 / (expected.size() - 1001);
        check(std::abs(frequency - PSG::DefaultClock / (32.0 * 254)) < 15, "PCM tone frequency");
        PSG quieter;
        mute(quieter);
        tone(quieter, 0, 254);
        quieter.write(0x96);
        quieter.runClocks(350000);
        quieter.takeSamples(part);
        check(std::abs(rms(part) / rms(expected) - std::pow(10.0, -0.6)) < 1e-5, "PCM amplitude follows attenuation");
        check(std::all_of(expected.begin(), expected.end(), [](float v)
                          { return std::isfinite(v) && std::abs(v) <= 1; }),
              "PCM must remain finite and normalized");

        PSG silent;
        mute(silent);
        silent.runClocks(PSG::DefaultClock);
        silent.takeSamples(part);
        check(part.size() == 12000 && std::all_of(part.begin(), part.end(), [](float v)
                                                  { return v == 0; }),
              "Unconsumed PCM storage must be bounded and silence exact");
        std::size_t samples = 0;
        silent.reset();
        mute(silent);
        for (unsigned i = 0; i < 1000; ++i)
        {
            silent.runClocks(i == 999 ? 3579 : 3580);
            silent.takeSamples(part);
            samples += part.size();
        }
        check(samples == (999ull * 3580 + 3579) * 48000 / PSG::DefaultClock, "No sample-count drift across many batches");
        bool rejected = false;
        try
        {
            PSG invalid(100, 101);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "Invalid clock/sample rate accepted");
        // Os ganhos do usuário alteram a mistura, mas osciladores e registradores continuam idênticos.
        PSG reference, mixed;
        R2SEGA8::Common::AudioSettings settings;
        settings.channels[0].enabled = false;
        settings.channels[1].volume = 50;
        settings.channels[2].volume = 25;
        settings.channels[3].volume = 75;
        mixed.setAudioSettings(settings);
        reference.runClocks(7696);
        mixed.runClocks(7696);
        const auto &a = reference.state();
        const auto &b = mixed.state();
        check(a.tone == b.tone && a.attenuation == b.attenuation && a.counter == b.counter && a.output == b.output &&
                  a.noiseShift == b.noiseShift && a.clocks == b.clocks && a.busyClocks == b.busyClocks,
              "Mixer settings must not alter emulated hardware");
        float mixedLevel = 0;
        for (unsigned channel = 0; channel < 4; ++channel)
            if (b.output[channel])
                mixedLevel += 0.25f * settings.channels[channel].gain();
        check(std::abs(mixed.level() - mixedLevel) < 1e-7, "Independent tone/noise gain and enable controls");
        PSG fullMaster, halfMaster;
        settings = {};
        settings.master.volume = 50;
        halfMaster.setAudioSettings(settings);
        fullMaster.runClocks(10000);
        halfMaster.runClocks(10000);
        fullMaster.takeSamples(expected);
        halfMaster.takeSamples(part);
        check(expected.size() == part.size(), "Master volume must preserve sample count");
        for (std::size_t i = 0; i < part.size(); ++i)
            check(part[i] == expected[i] * 0.5f, "Master gain must scale the complete mix");
        settings.master.enabled = false;
        halfMaster.setAudioSettings(settings);
        halfMaster.runClocks(10000);
        halfMaster.takeSamples(part);
        check(std::all_of(part.begin(), part.end(), [](float v)
                          { return v == 0; }),
              "Master mute must silence even filter tails");
        halfMaster.reset();
        halfMaster.runClocks(10000);
        halfMaster.takeSamples(part);
        check(std::all_of(part.begin(), part.end(), [](float v)
                          { return v == 0; }),
              "Hardware reset must preserve mixer preferences");
        settings.master.enabled = true;
        settings.master.volume = -10;
        check(settings.master.gain() == 0, "Negative presentation volume must clamp to zero");
        settings.master.volume = 200;
        check(settings.master.gain() == 1, "Presentation volume above 100 must clamp to unity");
        PSG savedPsg;
        tone(savedPsg, 0, 127);
        savedPsg.runClocks(12345);
        const auto snapshot = savedPsg.snapshot();
        savedPsg.takeSamples(part);
        savedPsg.runClocks(24680);
        savedPsg.takeSamples(expected);
        savedPsg.restore(snapshot);
        savedPsg.runClocks(24680);
        savedPsg.takeSamples(part);
        check(part == expected, "Restored PSG must reproduce subsequent PCM exactly");
        auto invalidSnapshot = snapshot;
        invalidSnapshot.state.attenuation[0] = 16;
        bool invalidRejected = false;
        try
        {
            savedPsg.restore(invalidSnapshot);
        }
        catch (const std::invalid_argument &)
        {
            invalidRejected = true;
        }
        check(invalidRejected && savedPsg.state().clocks == snapshot.state.clocks + 24680,
              "Invalid snapshot must not modify PSG");
        std::cout << "SN76489AN tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}