cmake_minimum_required(VERSION 3.16)
project(R2SEGA8 VERSION 0.0.1 LANGUAGES C CXX)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(R2SEGA8_BUILD_GUI "Build the SDL3 / Dear ImGui application" ON)

add_library(r2sega8_core STATIC
    src/Core/CPU/Z80/Z80.cpp
    src/Core/CPU/Z80/Z80Clock.cpp
    src/Common/RomFile.cpp
    src/Core/Cartridge/Cartridge.cpp
    src/Systems/Board.cpp
    src/Systems/SG1000/SG1000.cpp
    src/Systems/SC3000/SC3000.cpp
    src/Systems/MasterSystem/MasterSystem.cpp
    src/Systems/GameGear/GameGear.cpp
)
target_compile_features(r2sega8_core PUBLIC cxx_std_17)
target_include_directories(r2sega8_core PUBLIC src)
if(MSVC)
    target_compile_options(r2sega8_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(r2sega8_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(R2SEGA8_BUILD_GUI)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(SDL_CCACHE OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 829a65d769d935c4852f8159e964312c0957260a # release-3.4.18
    )
    FetchContent_MakeAvailable(SDL3)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab # v1.92.9b
    )
    FetchContent_MakeAvailable(imgui)
    add_library(r2sega8_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    target_include_directories(r2sega8_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(r2sega8_imgui PUBLIC SDL3::SDL3-static)
    add_library(r2sega8_window STATIC src/Window/Window.cpp src/Window/FileManager.cpp)
    target_compile_features(r2sega8_window PUBLIC cxx_std_17)
    target_include_directories(r2sega8_window PUBLIC src)
    target_link_libraries(r2sega8_window PRIVATE r2sega8_imgui)
    if(MSVC)
        target_compile_options(r2sega8_window PRIVATE /W4 /utf-8)
    else()
        target_compile_options(r2sega8_window PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_executable(R2SEGA8 src/Emulator.cpp src/Systems/Engine.cpp)
    target_link_libraries(R2SEGA8 PRIVATE r2sega8_window r2sega8_core)
    set_target_properties(R2SEGA8 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(z80_tests tests/Core/Z80Tests.cpp)
    target_link_libraries(z80_tests PRIVATE r2sega8_core)
    add_test(NAME z80_tests COMMAND z80_tests)
    add_executable(z80_vectors tests/Core/Z80Vectors.cpp)
    target_link_libraries(z80_vectors PRIVATE r2sega8_core)
    add_executable(z80_exerciser tests/Core/Z80Exerciser.cpp)
    target_link_libraries(z80_exerciser PRIVATE r2sega8_core)
    add_executable(rom_system_tests tests/Systems/RomSystemTests.cpp)
    target_link_libraries(rom_system_tests PRIVATE r2sega8_core)
    add_test(NAME rom_system_tests COMMAND rom_system_tests)
    if(R2SEGA8_BUILD_GUI)
        add_executable(window_smoke_tests tests/WindowSmokeTests.cpp)
        target_link_libraries(window_smoke_tests PRIVATE r2sega8_window r2sega8_imgui)
        add_test(NAME window_smoke_tests COMMAND window_smoke_tests)
        set_tests_properties(window_smoke_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    endif()
endif()
