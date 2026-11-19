cmake_minimum_required(VERSION 3.16)
project(PetriHD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(PETRIHD_GUI "Build the windowed app (needs SDL2)" ON)

# The simulation: deterministic, no dependencies.
add_library(petrihd_core core/world.cpp core/terrain.cpp core/file.cpp core/png.cpp)
target_include_directories(petrihd_core PUBLIC core)

# Terrain art: no GL, so headless screenshots match the window.
add_library(petrihd_art gfx/terrain_art.cpp)
target_include_directories(petrihd_art PUBLIC gfx)
target_link_libraries(petrihd_art PUBLIC petrihd_core)

add_executable(petrihd-run apps/petrihd_run.cpp)
target_link_libraries(petrihd-run PRIVATE petrihd_art)

enable_testing()
add_executable(petrihd_core_tests tests/core_test.cpp)
target_link_libraries(petrihd_core_tests PRIVATE petrihd_core)
add_test(NAME core COMMAND petrihd_core_tests)

if(PETRIHD_GUI)
    find_package(SDL2 REQUIRED)
    add_library(petrihd_gfx gfx/gl.cpp gfx/batch.cpp gfx/font.cpp gfx/world_view.cpp gfx/tilt_view.cpp)
    target_include_directories(petrihd_gfx PUBLIC gfx ui)
    target_link_libraries(petrihd_gfx PUBLIC SDL2::SDL2 petrihd_art)

    add_executable(petrihd apps/petrihd.cpp)
    target_link_libraries(petrihd PRIVATE petrihd_gfx)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(petrihd PRIVATE SDL2::SDL2main)
    endif()
endif()
