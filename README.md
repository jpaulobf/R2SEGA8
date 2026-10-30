# R2SEGA8

<div align="center">
  <img src="https://github.com/jpaulobf/R2SEGA8/blob/main/assets/r2sega8.png" height="450"/>
</div>

A Sega SG-1000, SC-3000, Master System and Game Gear emulator written from
scratch in C++, developed alongside R2NES / R2BOY.

The project is in early development. You can load ROMs, inspect memory and
run the SG-1000 CPU with background video output. Video support is
incomplete, and audio and controls are still being developed; games are not
fully supported yet.

## Opening a ROM

Use **File > Open**, **Ctrl+O**, or drag a file into the window. Select a ROM,
confirm the system and click **Load ROM**.

Open raw `.sg`, `.sc`, `.sms`, `.gg` or `.bin` files. Extract ZIP archives first.

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
- [Changelog](CHANGELOG.md)

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
