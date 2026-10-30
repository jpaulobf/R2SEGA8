#include "Engine.h"
#include "Audio.h"
#include "BoardFactory.h"
#include "Core/Audio/SN76489/SN76489.h"
#include "Input.h"
#include "FramePacer.h"
#include "Window/Window.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>

int R2SEGA8::Systems::Engine::run()
{
    try
    {
        std::unique_ptr<R2SEGA8::Core::Board> board;
        using Clock = std::chrono::steady_clock;
        R2SEGA8::Systems::FramePacer pacer;
        bool running = true;
        bool inputEnabled = false;
        bool resetAudio = false;
        R2SEGA8::Common::AudioSettings audioSettings;
        R2SEGA8::Systems::Input input;
        R2SEGA8::Window::Callbacks callbacks;
        callbacks.openRom = [&](const std::filesystem::path &file, R2SEGA8::Common::SystemType type)
        {
            auto next = R2SEGA8::Systems::createBoard(type, R2SEGA8::Core::Cartridge(file));
            next->setAudioSettings(audioSettings);
            board = std::move(next);
            resetAudio = true;
            input.clear();
            const auto &loaded = board->cartridge();
            const auto path = loaded.path().u8string();
            std::printf("System: %s\nFile: %s\nROM: %zu bytes | System RAM: %zu bytes\n", R2SEGA8::Common::systemName(board->type()),
                        path.c_str(), loaded.rom().size(), board->ram().size());
            if (!loaded.ram().empty())
                std::printf("Cartridge RAM: %zu bytes (The Castle profile)\n", loaded.ram().size());
            if (loaded.header())
                std::printf("Sega header: 0x%04X | Region: %X | Size code: %X\n", static_cast<unsigned>(loaded.header()->offset),
                            static_cast<unsigned>(loaded.header()->region), static_cast<unsigned>(loaded.header()->sizeCode));
            if (board->hasMemoryMap())
                std::puts("SG-1000: Z80 and TMS9918 video enabled (Graphics I/II, Text, Multicolor and sprites). Controllers enabled. "
                          "SN76489AN audio enabled. Cartridge banking is pending.");
            else
                std::puts("ROM loaded; system memory mapping is not implemented yet.");
            std::fflush(stdout);
        };
        callbacks.memory = [&]() -> R2SEGA8::Window::MemoryView
        {
            if (!board)
                return {};
            const auto &rom = board->cartridge().rom();
            const auto &ram = board->ram();
            return {{rom.data(), rom.size()}, {ram.data(), ram.size()}};
        };
        callbacks.exit = [&]()
        { running = false; };
        callbacks.hasRom = [&]()
        { return static_cast<bool>(board); };
        callbacks.reset = [&]()
        {
            if (!board)
                return;
            // Reconstrói toda a máquina com a imagem já carregada, sem reler o arquivo no disco.
            auto cartridge = board->cartridge();
            std::fill(cartridge.ram().begin(), cartridge.ram().end(), 0);
            auto next = R2SEGA8::Systems::createBoard(board->type(), std::move(cartridge));
            next->setAudioSettings(audioSettings);
            board = std::move(next);
            input.clear();
            resetAudio = true;
        };
        callbacks.unload = [&]()
        {
            board.reset();
            input.clear();
            resetAudio = true;
        };
        callbacks.audioSettings = [&]()
        { return audioSettings; };
        callbacks.setAudioSettings = [&](const R2SEGA8::Common::AudioSettings &settings)
        {
            audioSettings = settings;
            if (board)
                board->setAudioSettings(audioSettings);
        };
        callbacks.frame = [&]() -> R2SEGA8::Window::FrameView
        {
            if (!board)
                return {};
            const auto frame = board->videoFrame();
            return {frame.rgba, frame.width, frame.height, frame.pitch};
        };
        callbacks.inputEvent = [&](const SDL_Event &event, bool enabled)
        { input.handleEvent(event, enabled); };
        callbacks.inputAvailability = [&](bool enabled)
        {
            inputEnabled = enabled;
            if (!enabled)
            {
                input.clear();
                if (board)
                    board->setInput({});
            }
        };
        R2SEGA8::Window::Window window(std::move(callbacks), {256, 192});
        window.open();
        // A saída de áudio é destruída antes de a Window encerrar a sessão SDL.
        R2SEGA8::Systems::Audio audio;
        std::vector<float> audioSamples;
        if (!audio.initialize(R2SEGA8::Core::SN76489::DefaultSampleRate))
            std::cerr << "Audio: Playback unavailable: " << SDL_GetError() << '\n';
        // Limpa ações dos eventos e também as ações emitidas durante o desenho dos menus.
        const auto clearAudio = [&]()
        {
            if (!resetAudio)
                return;
            if (!audio.clear())
            {
                std::cerr << "Audio: Playback disabled: " << SDL_GetError() << '\n';
                audio.shutdown();
            }
            audioSamples.clear();
            const bool timed = board && board->clockFrequency() && board->cyclesPerFrame();
            pacer.reset(timed ? board->clockFrequency() : 60, timed ? board->cyclesPerFrame() : 1, Clock::now());
            resetAudio = false;
        };
        // A sessão termina antes de a janela chamar SDL_Quit, inclusive durante exceções.
        const auto stopInput = [](R2SEGA8::Systems::Input *session)
        { session->shutdown(); };
        const std::unique_ptr<R2SEGA8::Systems::Input, decltype(stopInput)> inputSession(&input, stopInput);
        if (!input.initialize())
            std::cerr << "Input: Gamepads unavailable: " << SDL_GetError() << '\n';
        while (running)
        {
            if (!window.processEvents())
                break;
            clearAudio();
            const auto buttons = input.update(inputEnabled);
            if (board)
                board->setInput(buttons);
            pacer.advance();
            if (board && board->clockFrequency() && board->cyclesPerFrame())
            {
                // Cada lote corresponde a um quadro físico completo. Recupera até quatro quadros
                // antes de desenhar, sem descartar clocks por pequenos atrasos do sistema operacional.
                for (unsigned frame = 0; frame < 4; ++frame)
                {
                    board->runCycles(board->cyclesPerFrame());
                    board->takeAudio(audioSamples);
                    if (!audio.submit(audioSamples.data(), audioSamples.size()))
                    {
                        std::cerr << "Audio: Playback disabled: " << SDL_GetError() << '\n';
                        audio.shutdown();
                    }
                    if (frame == 3 || Clock::now() < pacer.deadline())
                        break;
                    pacer.advance();
                }
            }
            window.render();
            clearAudio();
            std::this_thread::sleep_until(pacer.deadline());
            // Após uma pausa longa (arrastar janela, depurador), não executa uma fila ilimitada de quadros antigos.
            if (Clock::now() > pacer.deadline() + std::chrono::milliseconds(250))
            {
                const bool timed = board && board->clockFrequency() && board->cyclesPerFrame();
                pacer.reset(timed ? board->clockFrequency() : 60, timed ? board->cyclesPerFrame() : 1, Clock::now());
            }
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "R2SEGA8: " << error.what() << '\n';
        return 1;
    }
}