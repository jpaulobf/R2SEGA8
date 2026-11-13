#include "Engine.h"
#include "Audio.h"
#include "BoardFactory.h"
#include "Core/Audio/SN76489/SN76489.h"
#include "Core/CPU/Z80/Disassembler.h"
#include "Core/Systems/MasterSystem/MasterSystem.h"
#include "FrameCounter.h"
#include "FramePacer.h"
#include "Input.h"
#include "PauseState.h"
#include "RewindBuffer.h"
#include "SaveStateStore.h"
#include "Window/Window.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <set>
#include <cmath>
#include <utility>

namespace
{
    using namespace R2SEGA8;

    Systems::SaveStateStore saveStateStore()
    {
        char *raw = SDL_GetPrefPath("R2SEGA8", "R2SEGA8");
        if (!raw)
            throw std::runtime_error(SDL_GetError());
        const std::unique_ptr<char, decltype(&SDL_free)> path(raw, SDL_free);
        return Systems::SaveStateStore(std::filesystem::u8path(path.get()) / "saves");
    }

    std::unique_ptr<Core::Board> configuredBoard(Common::SystemType type, Core::Cartridge cartridge,
                                                 const Common::AudioSettings &audio,
                                                 const Common::ExecutionSettings &execution)
    {
        auto board = Systems::createBoard(type, std::move(cartridge), execution.consoleProfile);
        board->setAudioSettings(audio);
        board->setSpriteUnlimited(execution.unlimitedSpritesEnabled);
        board->setCPUOverclock(execution.cpuOverclockEnabled, execution.cpuMultiplier);
        board->setRenderLayers(execution.renderSprites, execution.renderTiles);
        return board;
    }

    void reportLoadedBoard(const Core::Board &board)
    {
        const auto &loaded = board.cartridge();
        const auto path = loaded.path().u8string();
        std::printf("System: %s\nFile: %s\nROM: %zu bytes | System RAM: %zu bytes\n", Common::systemName(board.type()),
                    path.c_str(), loaded.rom().size(), board.ram().size());
        if (!loaded.ram().empty())
            std::printf("Cartridge RAM: %zu bytes\n", loaded.ram().size());
        if (loaded.header())
            std::printf("Sega header: 0x%04X | Region: %X | Size code: %X\n", static_cast<unsigned>(loaded.header()->offset),
                        static_cast<unsigned>(loaded.header()->region), static_cast<unsigned>(loaded.header()->sizeCode));
        if (board.type() == Common::SystemType::SG1000)
            std::puts("SG-1000: Z80 and TMS9918 video enabled (Graphics I/II, Text, Multicolor and sprites). Controllers enabled. "
                      "SN76489AN audio enabled. Cartridge banking is pending.");
        else if (board.type() == Common::SystemType::MasterSystem && board.hasMemoryMap())
            std::printf("Master System: %s Mode 4 video, PSG audio, VDP interrupts and basic controllers enabled; other mappers are pending.\n",
                        board.consoleProfile() == Common::ConsoleProfile::PAL ? "PAL" : "NTSC");
        else if (board.type() == Common::SystemType::GameGear)
            std::puts("Game Gear: Z80, Sega mapper, initial VDP, controls and stereo audio enabled.");
        else
            std::puts("ROM loaded; system memory mapping is not implemented yet.");
        std::fflush(stdout);
    }
}

int R2SEGA8::Systems::Engine::run()
{
    try
    {
        std::unique_ptr<R2SEGA8::Core::Board> board;
        using Clock = std::chrono::steady_clock;
        R2SEGA8::Systems::FramePacer pacer;
        R2SEGA8::Systems::FrameCounter frameCounter;
        R2SEGA8::Systems::RewindBuffer rewind;
        bool running = true;
        bool inputEnabled = false;
        bool resetAudio = false;
        bool resetTiming = false;
        R2SEGA8::Systems::PauseState pause;
        bool emulationPaused = false;
        bool rewinding = false;
        bool debugActive = false, debugPaused = false, debugPauseRequested = false, debugSkipBreakpoint = false;
        std::set<std::uint16_t> breakpoints;
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
        // Só publica a máquina depois da construção ou restauração completa.
        const auto publishBoard = [&](std::unique_ptr<R2SEGA8::Core::Board> next)
        {
            board = std::move(next);
            debugPaused = debugPauseRequested = debugSkipBreakpoint = false;
            rewind.clear();
            input.clear();
            resetAudio = resetTiming = true;
            resetFPS();
        };
        R2SEGA8::Window::Callbacks callbacks;
        callbacks.openRom = [&](R2SEGA8::Core::Cartridge cartridge, R2SEGA8::Common::SystemType type)
        {
            auto next = configuredBoard(type, std::move(cartridge), audioSettings, executionSettings);
            publishBoard(std::move(next));
            reportLoadedBoard(*board);
        };
        callbacks.memory = [&]() -> R2SEGA8::Window::MemoryView
        {
            if (!board)
                return {};
            const auto &rom = board->cartridge().rom();
            const auto &ram = board->ram();
            return {{rom.data(), rom.size()}, {ram.data(), ram.size()}};
        };
        callbacks.debugOpen = [&]()
        { debugActive = true; };
        callbacks.debugClose = [&]()
        { debugActive = debugPaused = debugPauseRequested = debugSkipBreakpoint = false; resetAudio = resetTiming = true; };
        callbacks.debugView = [&]() -> R2SEGA8::Window::DebugView
        {
            if (!board || !board->hasMemoryMap())
                return {};
            return {true, debugPaused, board->cpu().state()};
        };
        callbacks.debugRead = [&](std::uint16_t address) -> std::uint8_t
        { return board && board->hasMemoryMap() ? board->readMemory(address) : 0xff; };
        callbacks.debugHasBreakpoint = [&](std::uint16_t address)
        { return breakpoints.count(address) != 0; };
        callbacks.debugBreakpoint = [&](std::uint16_t address)
        { if (!breakpoints.erase(address)) breakpoints.insert(address); };
        callbacks.debugClearBreakpoints = [&]()
        { breakpoints.clear(); };
        callbacks.debugPause = [&](bool value)
        {
            if (value)
                debugPauseRequested = true;
            else
            {
                debugPaused = debugPauseRequested = false;
                debugSkipBreakpoint = true;
                resetAudio = resetTiming = true;
            }
        };
        callbacks.debugStep = [&]()
        {
            if (!debugPaused || !board || !board->hasMemoryMap())
                return;
            const auto before = board->cpu().state();
            const auto d = R2SEGA8::Core::disassemble(before.pc, [&](std::uint16_t address)
                                                      { return board->readMemory(address); });
            std::printf("%04X  %-20s %-24s AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X CYC=%llu\n",
                        before.pc, d.bytes.c_str(), d.text.c_str(), before.a, before.f, before.b, before.c, before.d, before.e, before.h, before.l,
                        before.ix, before.iy, before.sp, static_cast<unsigned long long>(before.cycles));
            std::fflush(stdout);
            // Bound WAIT/BUSREQ so an external line cannot freeze the interface.
            unsigned ticks = 0;
            do
            {
                board->cpu().tick();
                ++ticks;
            } while (!board->cpu().instructionBoundary() && ticks < 1000000);
            if (!board->cpu().instructionBoundary())
                std::cerr << "Debugger: instruction did not finish within 1000000 T-states.\n";
            rewind.clear();
            resetAudio = resetTiming = true;
        };
        callbacks.exit = [&]()
        { running = false; };
        callbacks.hasRom = [&]()
        { return static_cast<bool>(board); };
        callbacks.canSaveState = [&]()
        { return board && board->supportsSaveState(); };
        const auto stateError = [](const std::exception &error)
        {
            std::cerr << "Save state: " << error.what() << '\n';
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save State", error.what(), nullptr);
        };
        callbacks.saveState = [&](unsigned slot)
        {
            try
            {
                if (!board)
                    throw std::runtime_error("Save state unavailable or invalid slot.");
                saveStateStore().save(*board, slot);
                std::cout << "Saved state to slot " << slot << '\n';
            }
            catch (const std::exception &error)
            {
                stateError(error);
            }
        };
        callbacks.loadState = [&](unsigned slot)
        {
            try
            {
                if (!board)
                    throw std::runtime_error("Save state unavailable or invalid slot.");
                const auto data = saveStateStore().load(*board, slot);
                auto next = configuredBoard(board->type(), board->cartridge(), audioSettings, executionSettings);
                next->loadState(data);
                next->setSpriteUnlimited(executionSettings.unlimitedSpritesEnabled);
                publishBoard(std::move(next));
                std::cout << "Loaded state from slot " << slot << '\n';
            }
            catch (const std::exception &error)
            {
                stateError(error);
            }
        };
        callbacks.reset = [&]()
        {
            if (!board)
                return;
            // Reconstrói toda a máquina com a imagem já carregada, sem reler o arquivo no disco.
            auto cartridge = board->cartridge();
            std::fill(cartridge.ram().begin(), cartridge.ram().end(), 0);
            auto next = configuredBoard(board->type(), std::move(cartridge), audioSettings, executionSettings);
            publishBoard(std::move(next));
        };
        callbacks.unload = [&]()
        {
            publishBoard(nullptr);
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
        callbacks.displayTransitionCompleted = [&]()
        { resetAudio = resetTiming = true; };
        callbacks.setAudioSettings = [&](const R2SEGA8::Common::AudioSettings &settings)
        {
            audioSettings = settings;
            if (board)
                board->setAudioSettings(audioSettings);
        };
        callbacks.consoleProfileAvailable = [&]()
        { return !board || board->consoleProfile().has_value(); };
        callbacks.executionSettings = [&]()
        { return executionSettings; };
        callbacks.fps = [&]()
        { return frameCounter.fps(); };
        callbacks.setExecutionSettings = [&](const R2SEGA8::Common::ExecutionSettings &settings)
        {
            if (settings.rewindEnabled != executionSettings.rewindEnabled || settings.rewindLevel != executionSettings.rewindLevel)
                rewind.clear();
            if (settings.cpuOverclockEnabled != executionSettings.cpuOverclockEnabled || settings.cpuMultiplier != executionSettings.cpuMultiplier)
                rewind.clear();
            const bool profileChanged = settings.consoleProfile != executionSettings.consoleProfile;
            if (profileChanged && board && board->consoleProfile())
            {
                auto cartridge = board->cartridge();
                std::fill(cartridge.ram().begin(), cartridge.ram().end(), 0);
                auto next = configuredBoard(board->type(), std::move(cartridge), audioSettings, settings);
                publishBoard(std::move(next));
                reportLoadedBoard(*board);
            }
            executionSettings = settings;
            if (board)
            {
                board->setSpriteUnlimited(settings.unlimitedSpritesEnabled);
                board->setCPUOverclock(settings.cpuOverclockEnabled, settings.cpuMultiplier);
                board->setRenderLayers(settings.renderSprites, settings.renderTiles);
            }
        };
        callbacks.frame = [&]() -> R2SEGA8::Window::FrameView
        {
            if (!board)
                return {};
            const auto frame = board->videoFrame();
            return {frame.rgba, frame.width, frame.height, frame.pitch};
        };
        callbacks.presentationSize = [&]() -> std::pair<unsigned, unsigned>
        {
            if (board && board->type() == R2SEGA8::Common::SystemType::GameGear)
                return {160, 144};
            return {256, 192};
        };
        // A Engine conecta o mapeamento confirmado à entrada do sistema emulado.
        callbacks.setKeyboardMapping = [&](const R2Keyboard::Mapping &mapping)
        { input.setKeyboardMapping(mapping); };
        callbacks.playerUsesJoystick = [&](unsigned player)
        { return input.playerUsesJoystick(player); };
        callbacks.setPlayerJoystick = [&](unsigned player, bool joystick)
        { input.setPlayerJoystick(player, joystick); };
        callbacks.inputEvent = [&](const SDL_Event &event, bool enabled)
        { input.handleEvent(event, enabled); };
        callbacks.lightPhaser = [&](int x, int y, bool trigger)
        {
            if (auto *sms = dynamic_cast<R2SEGA8::Core::MasterSystem *>(board.get()))
                sms->setLightPhaser(executionSettings.lightPhaserEnabled && !emulationPaused, x, y, trigger);
        };
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
            pause.setMinimized(window.isMinimized());
            pause.setDebugPaused(debugPaused);
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
            const bool nextRewinding = executionSettings.rewindEnabled && inputEnabled && !emulationPaused && !minimized &&
                                       timedBoard() && board->supportsSaveState() && input.isRewindPressed();
            if (nextRewinding != rewinding)
            {
                rewinding = nextRewinding;
                if (rewinding)
                    rewind.beginRewind();
                else
                    rewind.endRewind();
                resetAudio = resetTiming = true;
            }
            const bool fast = !rewinding && executionSettings.fastForwardEnabled && inputEnabled && !emulationPaused && !minimized &&
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
            const auto buttons = input.update(inputEnabled && !emulationPaused, executionSettings);
            if (board && !emulationPaused && !rewinding)
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
                        board->setInput(input.update(inputEnabled, executionSettings));
                    }
                    if (rewinding)
                    {
                        if (const auto *state = rewind.step())
                        {
                            board->loadState(*state);
                            board->setSpriteUnlimited(executionSettings.unlimitedSpritesEnabled);
                        }
                    }
                    else
                    {
                        if (executionSettings.rewindEnabled && board->supportsSaveState())
                        {
                            if (!rewind.configured())
                                rewind.configure(board->clockFrequency(), board->cyclesPerFrame(), executionSettings.rewindLevel);
                            if (rewind.captureDue())
                                rewind.push(board->saveState());
                        }
                        if (!debugActive || (breakpoints.empty() && !debugPauseRequested))
                        {
                            board->runCycles(board->cyclesPerFrame());
                            debugSkipBreakpoint = false;
                        }
                        else
                        {
                            const unsigned multiplier = executionSettings.cpuOverclockEnabled ? static_cast<unsigned>(std::lround(std::clamp(executionSettings.cpuMultiplier, 1.0f, 2.0f) * 100)) : 100;
                            const auto phase = ((board->cpu().state().cycles % multiplier) * 100) % multiplier;
                            const auto cycles = (static_cast<std::uint64_t>(board->cyclesPerFrame()) * multiplier + 99 - phase) / 100;
                            for (std::uint64_t i = 0; i < cycles; ++i)
                            {
                                auto &cpu = board->cpu();
                                if (cpu.instructionBoundary())
                                {
                                    if (debugPauseRequested || (!debugSkipBreakpoint && breakpoints.count(cpu.state().pc)))
                                    {
                                        debugPaused = true;
                                        debugPauseRequested = false;
                                        resetAudio = resetTiming = true;
                                        break;
                                    }
                                    debugSkipBreakpoint = false;
                                }
                                cpu.tick();
                            }
                        }
                        if (debugPaused)
                        {
                            synchronizePause();
                            break;
                        }
                        frameCounter.record(Clock::now());
                        board->takeAudio(audioSamples);
                        // O PSG continua avançando no fast-forward; apenas a saída PCM é descartada.
                        if (loopMode != LoopMode::FastForward && !audio.submit(audioSamples.data(), audioSamples.size(), board->audioChannels()))
                        {
                            std::cerr << "Audio: Playback disabled: " << SDL_GetError() << '\n';
                            audio.shutdown();
                        }
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