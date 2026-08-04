# MicroRender host test target.
#
# Standalone: this does not require the Pico SDK, Open Watcom, or DOSBox. It
# compiles the shared renderer core for the host so the same code that ships to
# DOS and RP2350 can be unit tested, fuzzed and benchmarked in CI.
#
#   cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# Sanitizers are on by default in Debug and off in Release, since the benchmark
# is meaningless under ASan. Override with -DMR_TESTS_SANITIZE=ON/OFF.

cmake_minimum_required(VERSION 3.16)
project(microrender_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(MR_SHARED_SRC "${CMAKE_CURRENT_LIST_DIR}/../shared/src")

if(NOT EXISTS "${MR_SHARED_SRC}/gfx.c")
    message(FATAL_ERROR
        "Cannot find the shared renderer at ${MR_SHARED_SRC}. "
        "Configure this directory from the repository root, e.g. "
        "cmake -S tests -B build-tests")
endif()

# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------

# GCC and Clang sanitizer builds are the default in Debug. MSVC ASan is
# opt-in: hosted VS2026 runners have been observed hanging in the tests,
# so normal Windows correctness testing must not silently enable it.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    set(MR_TESTS_SANITIZE_DEFAULT ON)
else()
    set(MR_TESTS_SANITIZE_DEFAULT OFF)
endif()

option(MR_TESTS_SANITIZE
    "Build the host tests with AddressSanitizer and UndefinedBehaviorSanitizer"
    ${MR_TESTS_SANITIZE_DEFAULT})
option(MR_TESTS_WERROR
    "Treat warnings in the shared renderer core as errors" ON)
option(MR_TESTS_INDEX8
    "Optional legacy indexed-color regression build; shipping frontends use RGB565"
    OFF)

set(MR_FUZZ_ITERATIONS 300 CACHE STRING
    "Iterations for the fuzz harness when run through ctest")
set(MR_BENCH_FRAMES 200 CACHE STRING
    "Frames per configuration for the benchmark")

# ---------------------------------------------------------------------------
# the shared renderer core, compiled for the host
# ---------------------------------------------------------------------------

add_library(microrender_core STATIC
    "${MR_SHARED_SRC}/gfx.c"
    "${MR_SHARED_SRC}/gfx_engine.c"
    "${MR_SHARED_SRC}/gfx_font5x7.c"
    "${MR_SHARED_SRC}/gfx_triangle.c"
    "${MR_SHARED_SRC}/mr_strbuf.c"
    "${MR_SHARED_SRC}/mr_game_demo.c"
)

target_include_directories(microrender_core PUBLIC "${MR_SHARED_SRC}")

if(MR_TESTS_INDEX8)
    target_compile_definitions(microrender_core PUBLIC GFX_COLOR_INDEX8=1)
endif()

# The core is expected to build silently under this set. It currently does; if
# a change breaks that, CI should say so loudly rather than accumulate noise.
if(MSVC)
    target_compile_options(microrender_core PRIVATE /W4)
    if(MR_TESTS_WERROR)
        target_compile_options(microrender_core PRIVATE /WX)
    endif()
else()
    target_compile_options(microrender_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef
    )
    if(MR_TESTS_WERROR)
        target_compile_options(microrender_core PRIVATE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# sanitizers
# ---------------------------------------------------------------------------

add_library(mr_sanitizers INTERFACE)

if(MR_TESTS_SANITIZE)
    if(MSVC)
        message(WARNING
            "MR_TESTS_SANITIZE is explicitly enabled under MSVC. Only "
            "AddressSanitizer is available (no UBSan), and some hosted "
            "VS2026 jobs have hung while running the tests.")
        target_compile_options(mr_sanitizers INTERFACE /fsanitize=address)
    else()
        target_compile_options(mr_sanitizers INTERFACE
            -fsanitize=address,undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer)
        target_link_options(mr_sanitizers INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# test binaries
# ---------------------------------------------------------------------------

function(mr_add_test_binary name source)
    add_executable(${name} "${source}")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(${name} PRIVATE microrender_core mr_sanitizers)
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
endfunction()

mr_add_test_binary(mr_test_unit  mr_test_unit.c)
mr_add_test_binary(mr_test_fuzz  mr_test_fuzz.c)
mr_add_test_binary(mr_test_game  mr_test_game.c)

# The benchmark measures the renderer, so it is always built optimised and
# never linked against sanitizers, whatever the build type is.
add_executable(mr_test_bench mr_test_bench.c)
target_include_directories(mr_test_bench PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(mr_test_bench PRIVATE microrender_core)
if(NOT MSVC)
    target_compile_options(mr_test_bench PRIVATE -O2 -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# ctest registration
# ---------------------------------------------------------------------------

enable_testing()

add_test(NAME unit COMMAND mr_test_unit)
set_tests_properties(unit PROPERTIES TIMEOUT 60 LABELS "unit")

add_test(NAME game COMMAND mr_test_game)
set_tests_properties(game PROPERTIES TIMEOUT 60 LABELS "game")

# Several seeds, so a regression that only shows up for particular geometry has
# somewhere to surface. Each is deterministic and reproducible from its seed.
# Every seed has its own timeout so a broken harness cannot occupy a CI runner
# for hours after another test has already timed out.
foreach(seed 0x5EED 0xC0FFEE 0xBADF00D 0x1337)
    add_test(NAME fuzz_${seed}
             COMMAND mr_test_fuzz ${MR_FUZZ_ITERATIONS} ${seed})
    set_tests_properties(fuzz_${seed} PROPERTIES TIMEOUT 120 LABELS "fuzz")
endforeach()

message(STATUS "MicroRender tests: build=${CMAKE_BUILD_TYPE} "
               "sanitize=${MR_TESTS_SANITIZE} index8=${MR_TESTS_INDEX8}")
