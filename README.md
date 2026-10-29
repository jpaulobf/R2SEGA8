# R2SEGA8

A Sega SG-1000, SC-3000, Master System and Game Gear emulator written from
scratch in C++, developed alongside R2NES / R2BOY.

R2SEGA8 is in its initial development stage. The SDL3 / Dear ImGui application
can open raw ROM images and inspect cartridge ROM and system RAM. CPU, video
and audio emulation are not implemented yet.

## Opening a ROM

Use **File > Open**, **Ctrl+O**, or drag a file into the window. The ImGui file
chooser supports folder navigation and a UTF-8 path field. On Windows, the
**Drive** selector lists available drive letters, including removable and mapped
drives. Select a file, confirm the system and click **Load ROM**.
Use **Go / Inspect** after typing a path.

Extensions `.sg`, `.sc`, `.sms` and `.gg` suggest a system. When a Sega header
identifies Master System or Game Gear, that hint takes precedence; conflicting
hints are displayed. You can always choose the system manually, including for
`.bin` files. Identification does not establish game compatibility.

ROM bytes are preserved without trimming or removing copier headers. ZIP
archives must be extracted first. The loader accepts nonempty raw images up to
16 MiB as an allocation limit, not a hardware compatibility claim.

The initial SG-1000 plain ROM profile supports up to 48 KiB and maps its 1 KiB
system RAM with mirroring. Cartridge RAM, banking and exact bus behavior are
pending. SC-3000, Master System and Game Gear currently allocate system RAM
and retain the cartridge for inspection; their memory maps are pending.

## Project structure

```text
src/
├── Common/
├── Core/
│   ├── CPU/
│   │   └── Z80/
│   ├── VDP/
│   │   ├── TMS9918/
│   │   └── SegaVDP/
│   ├── Audio/
│   │   ├── SN76489/
│   │   └── YM2413/
│   ├── Memory/
│   ├── Bus/
│   ├── Cartridge/
│   ├── Input/
│   └── Timing/
├── Systems/
│   ├── SG1000/
│   ├── SC3000/
│   ├── MasterSystem/
│   └── GameGear/
└── Window/
tests/
├── Core/
└── Systems/
docs/
```

`Common` contains shared utilities and system identifiers. `Core` contains
reusable emulation components, independent of SDL3. `Systems`
connects those components and defines each machine's memory map, I/O ports and
hardware configuration. `Window` contains the SDL3 session and the entire
Dear ImGui interface. Core tests run without SDL3 or a display.

See [docs/Architecture.md](docs/Architecture.md) for component responsibilities
and the proposed implementation sequence.

## Building

Requires **CMake 3.16+**, a **C++17 compiler** and **Git**. The first GUI
configuration downloads pinned SDL3 and Dear ImGui revisions and needs internet
access. Linux also needs the development packages for the chosen SDL video
backend (for example X11 or Wayland).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

For MinGW on Windows, add `-G Ninja` to the first command. The application is
generated in `bin/` (`bin/Debug/` with multi-configuration generators).

To build and test only the portable ROM and system code, without downloading
SDL3 or ImGui:

```sh
cmake -S . -B build-core -DR2SEGA8_BUILD_GUI=OFF
cmake --build build-core --config Debug --parallel
ctest --test-dir build-core -C Debug --output-on-failure
```

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).
