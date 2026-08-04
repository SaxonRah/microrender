cmake_minimum_required(VERSION 3.16)
project(microrender_raylib C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(MR_RAYLIB_PATH "" CACHE PATH "Path to a raylib source checkout, used when find_package(raylib) is unavailable")
set(MR_RAYLIB_DEFAULT_DEMO "game" CACHE STRING "Default demo: game or stress")
set_property(CACHE MR_RAYLIB_DEFAULT_DEMO PROPERTY STRINGS game stress)
set(MR_RAYLIB_DEFAULT_MODE "tiled" CACHE STRING "Default presentation: raw, tiled, lace, dirtyrect")
set_property(CACHE MR_RAYLIB_DEFAULT_MODE PROPERTY STRINGS raw tiled lace dirtyrect)
set(MR_RAYLIB_DEFAULT_TILE_H 240 CACHE STRING "Default renderer tile height")
set(MR_RAYLIB_DEFAULT_SCALE 3 CACHE STRING "Default integer window scale")
set(MR_RAYLIB_DEFAULT_SPRITES 512 CACHE STRING "Default stress sprite count")
set(MR_RAYLIB_DEFAULT_FPS 0 CACHE STRING "Default FPS cap; 0 is uncapped")
option(MR_RAYLIB_DEFAULT_AUTOPLAY "Enable scripted game input by default" OFF)
set(MR_RAYLIB_DEFAULT_LACE_BLOCK_H 4 CACHE STRING "Default temporal row-group height")

if(NOT MR_RAYLIB_DEFAULT_DEMO MATCHES "^(game|stress)$")
    message(FATAL_ERROR "MR_RAYLIB_DEFAULT_DEMO must be game or stress")
endif()
if(NOT MR_RAYLIB_DEFAULT_MODE MATCHES "^(raw|tiled|lace|dirtyrect)$")
    message(FATAL_ERROR "MR_RAYLIB_DEFAULT_MODE must be raw, tiled, lace, or dirtyrect")
endif()

set(MR_RAYLIB_DEMO_ID 0)
if(MR_RAYLIB_DEFAULT_DEMO STREQUAL "stress")
    set(MR_RAYLIB_DEMO_ID 1)
endif()
set(MR_RAYLIB_MODE_ID 1)
if(MR_RAYLIB_DEFAULT_MODE STREQUAL "raw")
    set(MR_RAYLIB_MODE_ID 0)
elseif(MR_RAYLIB_DEFAULT_MODE STREQUAL "lace")
    set(MR_RAYLIB_MODE_ID 2)
elseif(MR_RAYLIB_DEFAULT_MODE STREQUAL "dirtyrect")
    set(MR_RAYLIB_MODE_ID 3)
endif()

if(MR_RAYLIB_PATH)
    add_subdirectory("${MR_RAYLIB_PATH}" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)
else()
    find_package(raylib CONFIG QUIET)
endif()

if(NOT TARGET raylib)
    find_path(RAYLIB_INCLUDE_DIR raylib.h)
    find_library(RAYLIB_LIBRARY NAMES raylib)
    if(RAYLIB_INCLUDE_DIR AND RAYLIB_LIBRARY)
        add_library(raylib UNKNOWN IMPORTED)
        set_target_properties(raylib PROPERTIES
            IMPORTED_LOCATION "${RAYLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_INCLUDE_DIR}")
    endif()
endif()

if(NOT TARGET raylib)
    message(FATAL_ERROR
        "raylib was not found. Install raylib, set CMAKE_PREFIX_PATH, or pass "
        "-DMR_RAYLIB_PATH=C:/path/to/raylib")
endif()

set(MR_SHARED_SRC "${CMAKE_CURRENT_LIST_DIR}/../shared/src")
add_executable(microrender_raylib
    main.c
    "${MR_SHARED_SRC}/gfx.c"
    "${MR_SHARED_SRC}/gfx_engine.c"
    "${MR_SHARED_SRC}/gfx_font5x7.c"
    "${MR_SHARED_SRC}/gfx_triangle.c"
    "${MR_SHARED_SRC}/mr_strbuf.c"
    "${MR_SHARED_SRC}/mr_autodemo.c"
    "${MR_SHARED_SRC}/mr_game_demo.c"
    "${MR_SHARED_SRC}/mr_stress_test.c")

target_include_directories(microrender_raylib PRIVATE "${MR_SHARED_SRC}")
target_link_libraries(microrender_raylib PRIVATE raylib)
target_compile_definitions(microrender_raylib PRIVATE
    GFX_COLOR_INDEX8=0
    MR_RAYLIB_DEFAULT_DEMO=${MR_RAYLIB_DEMO_ID}
    MR_RAYLIB_DEFAULT_MODE=${MR_RAYLIB_MODE_ID}
    MR_RAYLIB_DEFAULT_TILE_H=${MR_RAYLIB_DEFAULT_TILE_H}
    MR_RAYLIB_DEFAULT_SCALE=${MR_RAYLIB_DEFAULT_SCALE}
    MR_RAYLIB_DEFAULT_SPRITES=${MR_RAYLIB_DEFAULT_SPRITES}
    MR_RAYLIB_DEFAULT_FPS=${MR_RAYLIB_DEFAULT_FPS}
    MR_RAYLIB_DEFAULT_AUTOPLAY=$<BOOL:${MR_RAYLIB_DEFAULT_AUTOPLAY}>
    MR_RAYLIB_DEFAULT_LACE_BLOCK_H=${MR_RAYLIB_DEFAULT_LACE_BLOCK_H})

if(MSVC)
    target_compile_options(microrender_raylib PRIVATE /O2 /W4)
else()
    target_compile_options(microrender_raylib PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

message(STATUS "MicroRender Raylib: 320x240 RGB565 demo=${MR_RAYLIB_DEFAULT_DEMO} mode=${MR_RAYLIB_DEFAULT_MODE} tile_h=${MR_RAYLIB_DEFAULT_TILE_H} fps=${MR_RAYLIB_DEFAULT_FPS} autoplay=${MR_RAYLIB_DEFAULT_AUTOPLAY}")
