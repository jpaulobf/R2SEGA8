# R2SEGA8

<div align="center">
  <img src="https://github.com/jpaulobf/R2SEGA8/blob/main/assets/r2sega8.png" height="380"/>
</div>

<br>
A Sega SG-1000, SC-3000, Master System and Game Gear emulator written from
scratch in C++, developed alongside R2NES / R2BOY.

The project is in early development. You can load ROMs and
run SG-1000 games with backgrounds, sprites, controllers and PSG audio.
Compatibility is still being improved; games are not fully supported yet.

## Opening a ROM

Use **File > Open** or **Ctrl+O**, then click a ROM to load it immediately.
Dragging a ROM into the window also loads it directly.

Use **File > Reset** or **F12** to restart the current ROM, and **File > Unload**
to stop emulation and close the cartridge. Audio preferences stay unchanged.

Press **Pause/Break** to pause or resume. Opening a menu pauses emulation;
closing it resumes only if you did not pause manually.

The extension selects the system: `.sg`, `.sc`, `.sms` or `.gg` (case-insensitive).
ZIP loading and automatic identification of `.bin` files will come later.

## Display

Starts at **3X**. Choose **1X–5X** in **Display**, or press **Ctrl+1–5**.
The video fills the available area while keeping its aspect ratio.
Maximizing is allowed; manual resizing is blocked.

**Display > VSync** synchronizes presentation with the monitor; it starts enabled.
Hold **Tab** for silent fast-forward. **Hacks > Enable Fast-Forward** starts enabled.
The title shows emulated FPS before the ROM filename.

## Controls

Use **WASD or arrow keys** to move, **J/K** for buttons **1/2**, and **Enter**
for the SG-1000 Pause/Hold button. **U/I** provide turbo for buttons **1/2**.

SDL-compatible gamepads are detected automatically, including connection and
removal while running. Two players are supported. See the [input guide](docs/Input.md)
for gamepad positions and keyboard mappings.

## Building

Requires **CMake 3.16+**, a **C++17 compiler** and **Git**. The first build
needs internet access to download dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

The application is generated in `bin/` (or `bin/Debug/`). See the
[build guide](docs/Building.md) for platform notes and builds without a GUI.

## Documentation

- [Architecture and development roadmap](docs/Architecture.md)
- [Z80 implementation and validation](docs/Z80.md)
- [Initial TMS9918 video and demo ROM](docs/TMS9918.md)
- [SN76489AN audio](docs/SN76489.md)
- [Display options](docs/Display.md)
- [Changelog](CHANGELOG.md)

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
