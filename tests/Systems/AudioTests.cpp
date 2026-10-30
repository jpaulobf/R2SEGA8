#include "Systems/Audio.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    // Interrompe a validação com uma descrição do contrato esperado da saída SDL.
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
        using R2SEGA8::Systems::Audio;
        // Blocos maiores que a preparação de 40 ms tornam visível a diferença entre entrada e saída.
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "4096", SDL_HINT_OVERRIDE);
        Audio audio;
        std::vector<float> samples(10000, 0.1f);
        check(audio.submit(samples.data(), samples.size()) && audio.clear(), "Disabled output must discard PCM safely");
        check(!audio.initialize(0), "Invalid sample rate accepted");
        check(audio.initialize(48000), SDL_GetError());
        check(!audio.playing() && audio.queuedSamples() == 0, "Device must start paused and empty");
        check(!audio.submit(nullptr, 1), "Null PCM data accepted");
        check(audio.submit(samples.data(), 800) && audio.queuedSamples() == 800 && !audio.playing(),
              "One emulated frame must be queued without starting an underfilled device");
        check(audio.submit(samples.data(), 800) && !audio.playing(), "Priming must continue below 40 ms");
        check(audio.submit(samples.data(), 800) && audio.playing(), "Priming threshold must resume playback");
        check(audio.clear() && !audio.playing() && audio.queuedSamples() == 0, "ROM replacement must pause and drop old PCM");
        check(audio.submit(samples.data(), samples.size()) && audio.playing() && audio.queuedSamples() <= 5760,
              "Oversized batches must be bounded to 120 ms");
        SDL_Delay(250);
        check(audio.queuedSamples() == 0, "Dummy device must consume the queued samples");
        check(audio.submit(samples.data(), 800) && audio.playing(),
              "An empty input queue must not pause playback or insert another priming gap");
        audio.shutdown();
        audio.shutdown();
        check(!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO), "Shutdown must release the audio subsystem reference");
        check(audio.initialize(48000) && audio.clear(), "Session must support reopening after shutdown");
        audio.shutdown();
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "r2sega8-nonexistent-driver", SDL_HINT_OVERRIDE);
        check(!audio.initialize(48000) && !(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO),
              "Audio initialization failure must not leave a subsystem reference");
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
        check(audio.initialize(48000), "Session must recover after initialization failure");
        std::cout << "SDL audio tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}