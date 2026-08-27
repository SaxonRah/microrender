cmake_minimum_required(VERSION 3.16)
project(microrender_shared C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MR_GFX_INDEX8 "Build the standalone renderer in legacy INDEX8 mode" OFF)
option(MR_BUILD_ENGINE_HELPERS
       "Also build the optional actor/camera/collision helper layer" OFF)

# This is the actual rendering library. It deliberately contains no clock,
# input, game, stress-test, window-system, DOS, or Pico code.
add_library(microrender_gfx STATIC
    src/gfx.c
    src/gfx_font5x7.c
    src/gfx_triangle.c
    src/gfx_rgb444.c)

add_library(microrender::gfx ALIAS microrender_gfx)
target_include_directories(microrender_gfx PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src")

if(MR_GFX_INDEX8)
    target_compile_definitions(microrender_gfx PUBLIC GFX_COLOR_INDEX8=1)
endif()

if(MSVC)
    target_compile_options(microrender_gfx PRIVATE /W4)
else()
    target_compile_options(microrender_gfx PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef)
endif()

# Convenience helpers are intentionally a separate target so an application
# can use the renderer without also accepting actor/collision/camera policy.
if(MR_BUILD_ENGINE_HELPERS)
    add_library(microrender_engine STATIC src/gfx_engine.c)
    add_library(microrender::engine ALIAS microrender_engine)
    target_include_directories(microrender_engine PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src")
    target_link_libraries(microrender_engine PUBLIC microrender_gfx)
    if(MSVC)
        target_compile_options(microrender_engine PRIVATE /W4)
    else()
        target_compile_options(microrender_engine PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion
            -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef)
    endif()
endif()
