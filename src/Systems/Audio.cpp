#include "Audio.h"
#include <algorithm>
#include <string>

namespace R2SEGA8::Systems
{
    Audio::~Audio()
    {
        shutdown();
    }

    bool Audio::initialize(unsigned sampleRate)
    {
        shutdown();
        if (sampleRate < 1000 || sampleRate > 192000)
            return SDL_SetError("Invalid audio sample rate.");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return false;
        initialized_ = true;
        sampleRate_ = sampleRate;
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(sampleRate)};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_)
        {
            const std::string error = SDL_GetError();
            shutdown();
            SDL_SetError("%s", error.c_str());
            return false;
        }
        return true;
    }

    void Audio::shutdown()
    {
        if (stream_)
            SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_ = playing_ = false;
        sampleRate_ = 0;
    }

    std::size_t Audio::queuedSamples() const
    {
        const int bytes = stream_ ? SDL_GetAudioStreamQueued(stream_) : 0;
        return bytes > 0 ? static_cast<std::size_t>(bytes) / (2 * sizeof(float)) : 0;
    }

    bool Audio::clear()
    {
        if (!stream_)
            return true;
        const bool paused = SDL_PauseAudioStreamDevice(stream_);
        const bool cleared = SDL_ClearAudioStream(stream_);
        playing_ = false;
        return paused && cleared;
    }

    bool Audio::submit(const float *samples, std::size_t count, unsigned channels)
    {
        if (!stream_ || !count)
            return true;
        if (!samples)
            return SDL_SetError("Missing audio sample data.");
        if ((channels != 1 && channels != 2) || count % channels != 0)
            return SDL_SetError("Invalid audio channel count or incomplete frame.");
        std::size_t frames = count / channels;
        const int bytes = SDL_GetAudioStreamQueued(stream_);
        if (bytes < 0)
            return false;
        const std::size_t queued = static_cast<std::size_t>(bytes) / (2 * sizeof(float));
        const std::size_t limit = sampleRate_ * 120 / 1000;
        // Fila de entrada vazia não prova underrun: o dispositivo pode ter consumido um bloco grande.
        // Continua anexando PCM, sem pausar nem inserir outros 40 ms de preparação.
        if (queued + std::min(frames, limit) > limit)
            if (!clear())
                return false;
        // Um consumidor atrasado não deve acumular segundos de som antigo.
        if (frames > limit)
        {
            samples += (frames - limit) * channels;
            frames = limit;
        }
        // Duplica cada amostra mono para a saída estéreo sem alterar o volume.
        if (channels == 1)
        {
            stereoBuffer_.resize(frames * 2);
            for (std::size_t i = 0; i < frames; ++i)
                stereoBuffer_[i * 2] = stereoBuffer_[i * 2 + 1] = samples[i];
            samples = stereoBuffer_.data();
        }
        if (!SDL_PutAudioStreamData(stream_, samples, static_cast<int>(frames * 2 * sizeof(float))))
            return false;
        if (!playing_ && queuedSamples() >= sampleRate_ * 40 / 1000)
        {
            if (!SDL_ResumeAudioStreamDevice(stream_))
                return false;
            playing_ = true;
        }
        return true;
    }
}