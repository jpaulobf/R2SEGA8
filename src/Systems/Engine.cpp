#include "Engine.h"
#include "Audio.h"
#include "BoardFactory.h"
#include "Core/Audio/SN76489/SN76489.h"
#include "FrameCounter.h"
#include "FramePacer.h"
#include "Input.h"
#include "PauseState.h"
#include "Window/Window.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

int R2SEGA8::Systems::Engine::run()
{
    try
    {
        std::unique_ptr<R2SEGA8::Core::Board> board;
        using Clock = std::chrono::steady_clock;
        R2SEGA8::Systems::FramePacer pacer;
        R2SEGA8::Systems::FrameCounter frameCounter;
        bool running = true;
        bool inputEnabled = false;
        bool resetAudio = false;
        bool resetTiming = false;
        R2SEGA8::Systems::PauseState pause;
        bool emulationPaused = false;
        auto pauseStarted = Clock::now();
        // Uma nova máquina não herda o tempo de pausa usado na medição da máquina anterior.
        const auto resetFPS = [&]()
        {
            pauseStarted = Clock::now();
            frameCounter.reset(pauseStarted);
        };
        enum class LoopMode
        {
            Hardware,
            VSync,
            FastForward
        };
        LoopMode loopMode = LoopMode::Hardware;
        R2SEGA8::Common::ExecutionSettings executionSettings;
        R2SEGA8::Common::AudioSettings audioSettings;
        R2SEGA8::Systems::Input input;
        R2SEGA8::Window::Callbacks callbacks;
        callbacks.openRom = [&](const std::filesystem::path &file, R2SEGA8::Common::SystemType type)
        {
            auto next = R2SEGA8::Systems::createBoard(type, R2SEGA8::Core::Cartridge(file));
            next->setAudioSettings(audioSettings);
            board = std::move(next);
            resetAudio = true;
            resetFPS();
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
            resetFPS();
        };
        callbacks.unload = [&]()
        {
            board.reset();
            input.clear();
            resetAudio = true;
            resetFPS();
        };
        callbacks.audioSettings = [&]()
        { return audioSettings; };
        callbacks.togglePause = [&]()
        {
            if (board)
                pause.toggleManual();
        };
        callbacks.menuPause = [&](bool paused)
        { pause.setMenuPaused(paused); };
        callbacks.setAudioSettings = [&](const R2SEGA8::Common::AudioSettings &settings)
        {
            audioSettings = settings;
            if (board)
                board->setAudioSettings(audioSettings);
        };
        callbacks.executionSettings = [&]()
        { return executionSettings; };
        callbacks.fps = [&]()
        { return frameCounter.fps(); };
        callbacks.setExecutionSettings = [&](const R2SEGA8::Common::ExecutionSettings &settings)
        { executionSettings = settings; };
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
        // Consulta se a placa fornece uma razão de clocks válida para o limitador nativo.
        const auto timedBoard = [&]()
        { return board && board->clockFrequency() && board->cyclesPerFrame(); };
        // Apenas mudanças da pausa efetiva descartam áudio e prazos; fechar menu não desfaz Pause/Break.
        const auto synchronizePause = [&]()
        {
            const bool next = pause.paused();
            if (next == emulationPaused)
                return;
            const auto now = Clock::now();
            if (next)
                pauseStarted = now;
            else
                frameCounter.excludePause(now - pauseStarted);
            emulationPaused = next;
            input.clear();
            if (board)
                board->setInput({});
            resetAudio = resetTiming = true;
        };
        // As preferências sobrevivem ao TAB: somente o modo efetivo suspende VSync e saída de áudio.
        const auto synchronizeLoop = [&]()
        {
            synchronizePause();
            const bool minimized = window.isMinimized();
            const bool fast = executionSettings.fastForwardEnabled && inputEnabled && !emulationPaused && !minimized &&
                              input.isFastForwardPressed() && timedBoard();
            auto nextMode = fast ? LoopMode::FastForward : executionSettings.vsync && !minimized ? LoopMode::VSync
                                                                                                 : LoopMode::Hardware;
            if (nextMode == loopMode)
                return;
            if (!window.setVSync(nextMode == LoopMode::VSync))
            {
                // Um backend sem VSync mantém a execução pelo clock original, sem encerrar a sessão.
                if (nextMode != LoopMode::VSync)
                    throw std::runtime_error(std::string("Unable to disable VSync: ") + SDL_GetError());
                std::cerr << "Display: VSync unavailable: " << SDL_GetError() << '\n';
                executionSettings.vsync = false;
                nextMode = LoopMode::Hardware;
            }
            // Descarta PCM anterior tanto na entrada quanto na saída do avanço rápido.
            if (loopMode == LoopMode::FastForward || nextMode == LoopMode::FastForward)
                resetAudio = true;
            loopMode = nextMode;
            resetTiming = true;
        };
        // Consome ações dos eventos e também as emitidas pelos menus; nenhum modo herda prazos antigos.
        const auto resetRuntime = [&]()
        {
            if (!resetAudio && !resetTiming)
                return;
            if (resetAudio)
            {
                if (!audio.clear())
                {
                    std::cerr << "Audio: Playback disabled: " << SDL_GetError() << '\n';
                    audio.shutdown();
                }
                audioSamples.clear();
            }
            const bool timed = timedBoard();
            pacer.reset(timed ? board->clockFrequency() : 60, timed ? board->cyclesPerFrame() : 1, Clock::now());
            resetAudio = resetTiming = false;
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
            synchronizeLoop();
            resetRuntime();
            const auto buttons = input.update(inputEnabled && !emulationPaused);
            if (board && !emulationPaused)
                board->setInput(buttons);
            if (loopMode == LoopMode::Hardware)
                pacer.advance();
            if (timedBoard() && !emulationPaused)
            {
                // Sem limite, vários quadros por apresentação reduzem a dependência do custo do renderer.
                // O modo nativo continua recuperando até quatro quadros; VSync apresenta cada quadro.
                const unsigned framesPerBatch = loopMode == LoopMode::FastForward ? 8 : 4;
                for (unsigned frame = 0; frame < framesPerBatch; ++frame)
                {
                    if (frame && loopMode == LoopMode::FastForward)
                    {
                        // Consulta TAB e ações entre quadros, sem esperar a apresentação do lote inteiro.
                        if (!window.processEvents())
                        {
                            running = false;
                            break;
                        }
                        synchronizeLoop();
                        resetRuntime();
                        if (emulationPaused || loopMode != LoopMode::FastForward || !timedBoard())
                            break;
                        board->setInput(input.update(inputEnabled));
                    }
                    board->runCycles(board->cyclesPerFrame());
                    frameCounter.record(Clock::now());
                    board->takeAudio(audioSamples);
                    // O PSG continua avançando no fast-forward; apenas a saída PCM é descartada.
                    if (loopMode != LoopMode::FastForward && !audio.submit(audioSamples.data(), audioSamples.size()))
                    {
                        std::cerr << "Audio: Playback disabled: " << SDL_GetError() << '\n';
                        audio.shutdown();
                    }
                    if (loopMode == LoopMode::FastForward)
                        continue;
                    if (loopMode == LoopMode::VSync || frame == 3 || Clock::now() < pacer.deadline())
                        break;
                    pacer.advance();
                }
            }
            if (!running)
                break;
            window.render();
            synchronizeLoop();
            resetRuntime();
            if (loopMode != LoopMode::Hardware)
                continue;
            std::this_thread::sleep_until(pacer.deadline());
            // Após uma pausa longa (arrastar janela, depurador), não executa uma fila ilimitada de quadros antigos.
            if (Clock::now() > pacer.deadline() + std::chrono::milliseconds(250))
            {
                const bool timed = timedBoard();
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