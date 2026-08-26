# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# ---------------------------------------------------------------------------
# MicroRender / RP2350 build
#
# Every build variant is selected through the cache variables below. Nothing in
# this file is rewritten by a script. shared/tools/set_pico_stress_build.py and
# shared/tools/install_stress_overlay.py used to mutate this file and
# microrender.c in place and leave .bak files behind, which put the build
# configuration in the working tree instead of in version control.
#
# Configure a variant like this instead:
#
#   cmake -S microrender -B microrender/build \
#         -DMR_APP=STRESS -DMR_STRESS_MODE=lace \
#         -DMR_STRESS_SPRITES=1024 -DMR_PICO_SYS_KHZ=300000
#
# or from the Pico VS Code extension by setting the same variables under
# cmake.configureSettings in .vscode/settings.json.
# ---------------------------------------------------------------------------

set(PICO_BOARD pimoroni_pico_plus2_rp2350 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(microrender C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---------------------------------------------------------------------------
# what to build
# ---------------------------------------------------------------------------

set(MR_APP "GAME" CACHE STRING "Which application to build: GAME or STRESS")
set_property(CACHE MR_APP PROPERTY STRINGS GAME STRESS)

if(NOT MR_APP STREQUAL "GAME" AND NOT MR_APP STREQUAL "STRESS")
    message(FATAL_ERROR "MR_APP must be GAME or STRESS, got '${MR_APP}'")
endif()

set(MR_GAME_PRESENTATION "pipelined" CACHE STRING
    "Game presentation: raw or pipelined")
set_property(CACHE MR_GAME_PRESENTATION PROPERTY STRINGS raw pipelined)
if(MR_GAME_PRESENTATION STREQUAL "raw")
    set(MR_GAME_PRESENT_MODE 0)
elseif(MR_GAME_PRESENTATION STREQUAL "pipelined")
    set(MR_GAME_PRESENT_MODE 1)
else()
    message(FATAL_ERROR
        "MR_GAME_PRESENTATION must be raw or pipelined, got '${MR_GAME_PRESENTATION}'")
endif()

# ---------------------------------------------------------------------------
# display / panel
# ---------------------------------------------------------------------------

set(MR_SCREEN_W       320      CACHE STRING "Render width in pixels")
set(MR_SCREEN_H       240      CACHE STRING "Render height in pixels")
set(MR_TILE_H         240      CACHE STRING "Tile height in rows; equal to MR_SCREEN_H gives one full-screen DMA per frame")
set(MR_VIEW_H         240      CACHE STRING "Active flushed height; may be less than MR_SCREEN_H to cut payload")
set(MR_LCD_PIN_MISO   4        CACHE STRING "ILI9341 MISO pin")
set(MR_LCD_PIN_CS     5        CACHE STRING "ILI9341 CS pin")
set(MR_LCD_PIN_SCK    6        CACHE STRING "ILI9341 SCK pin")
set(MR_LCD_PIN_MOSI   7        CACHE STRING "ILI9341 MOSI pin")
set(MR_LCD_PIN_RST    8        CACHE STRING "ILI9341 reset pin")
set(MR_LCD_PIN_DC     9        CACHE STRING "ILI9341 data/command pin")
set(MR_LCD_SPI_BAUD   75000000 CACHE STRING "SPI clock in Hz; 75 MHz is the known-stable starting point")
set(MR_ILI9341_MADCTL 0xE8     CACHE STRING "MADCTL orientation byte")
# Panel refresh rate (FRMCTR1). Defaults reproduce the ILI9341 reset state,
# about 70 Hz. Lower RTNA is faster: 0x19=76Hz, 0x16=86Hz, 0x13=100Hz,
# 0x10=119Hz. Only meaningful if the presentation rate actually exceeds 70 FPS.
option(MR_ILI9341_COLMOD_12BIT "Drive the panel at 12bpp (RGB444) instead of 16bpp" OFF)
set(MR_STRESS_LACE_PHASES 2 CACHE STRING "Lace phases; 1 = every row every frame")
set(MR_ILI9341_FRMCTR1_DIVA 0x00 CACHE STRING "FRMCTR1 fosc division ratio (0-3)")
set(MR_ILI9341_FRMCTR1_RTNA 0x1B CACHE STRING "FRMCTR1 clocks per line; lower is faster")

# ---------------------------------------------------------------------------
# clocks
# ---------------------------------------------------------------------------

set(MR_PICO_SYS_KHZ      300000 CACHE STRING "System clock in kHz")
set(MR_PICO_PERI_PLL_KHZ 0      CACHE STRING "Peripheral PLL in kHz; 0 derives it from the system clock")
option(MR_PICO_PERI_FROM_SYS  "Drive the peripheral clock from the system clock" ON)
option(MR_PICO_CLOCK_REQUIRED "Fail at runtime if the requested system clock cannot be set" OFF)
option(MR_PICO_FRAME_PIPELINE "Overlap tile rasterization with LCD DMA using a second tile buffer" ON)
# Lace presents alternating row groups with the blocking flush, so rasterization
# and transfer are strictly serial. Handing the panel to core 1 overlaps them,
# at the cost of reinstating the 150 KiB second frame buffer that lace normally
# skips (300 KiB total of 520 KiB). Only affects MR_STRESS_MODE=lace/lacefixed.
option(MR_PICO_PRESENT_CORE1 "Present lace row groups from core 1 while core 0 renders the next frame" OFF)
option(MR_PICO_SCREENSHOT "Enable the shared USB RGB565 screenshot service for every Pico app" ON)
option(MR_PICO_GAME_SERIAL "Enable game USB FPS logs" OFF)

# ---------------------------------------------------------------------------
# stress test configuration (ignored when MR_APP is GAME)
# ---------------------------------------------------------------------------

set(MR_STRESS_MODE "visible" CACHE STRING
    "Presentation mode: raw, visible, render, fixedrender, everyN, dirty, dirtyfixed, dirtyrect, dirtyrectfixed, lace, lacefixed, lcdtest")
set_property(CACHE MR_STRESS_MODE PROPERTY STRINGS
    raw visible render fixedrender everyN dirty dirtyfixed dirtyrect dirtyrectfixed lace lacefixed lcdtest)

set(MR_STRESS_SPRITES    1024 CACHE STRING "Sprites per frame")
set(MR_STRESS_TARGET_FPS 60   CACHE STRING "Frame pacing target")
set(MR_STRESS_STATS_RATE 8    CACHE STRING "Stats sampling rate in frames")
set(MR_STRESS_LCD_EVERY  4    CACHE STRING "Flush the LCD every N frames in everyN mode")
set(MR_STRESS_HUD_MODE   2    CACHE STRING "HUD: 0 off, 1 full, 2 one-line")
set(MR_STRESS_DIRTY_MERGE_GAP          4  CACHE STRING "Merge dirty rects separated by fewer than N pixels")
set(MR_STRESS_DIRTY_RECT_MAX_H         16 CACHE STRING "Maximum dirty rect height in rows")
set(MR_STRESS_DIRTY_FULL_THRESHOLD_PCT 70 CACHE STRING "Promote to a full redraw above this percentage of screen area")
set(MR_STRESS_LACE_BLOCK_H             4  CACHE STRING "Row-group height for temporal presentation")

# Older mr.bat revisions could write an empty command-line value into an
# existing CMake cache.  CACHE defaults do not replace an already-cached empty
# string, which would later produce -DMR_STRESS_PICO_LACE_BLOCK_H="" and an
# invalid C expression.  Repair that stale state during configuration.
if("${MR_STRESS_LACE_BLOCK_H}" STREQUAL "")
    message(STATUS "MicroRender: resetting empty MR_STRESS_LACE_BLOCK_H cache value to 4")
    set(MR_STRESS_LACE_BLOCK_H 4 CACHE STRING
        "Row-group height for temporal presentation" FORCE)
endif()
if(NOT "${MR_STRESS_LACE_BLOCK_H}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "MR_STRESS_LACE_BLOCK_H must be a positive integer; got '${MR_STRESS_LACE_BLOCK_H}'")
endif()

option(MR_STRESS_ENABLE_TRIANGLES "Draw triangles in the stress scene" ON)
option(MR_STRESS_CACHE_FIXED_BG   "Cache the static background in fixed-camera modes" OFF)
option(MR_STRESS_FAST_METRICS     "Use the cheap metrics path" ON)
option(MR_STRESS_PICO_DIAG        "Draw the Pico diagnostic footer" OFF)
option(MR_STRESS_PICO_SERIAL      "Enable USB serial output" OFF)
set(MR_STRESS_PICO_WAIT_USB_MS 0 CACHE STRING "Milliseconds to wait for USB enumeration at boot")

# Translate the friendly mode name into the numeric flush mode plus the
# orthogonal fixed-camera flag that the stress demo actually reads.
set(MR_STRESS_FIXED_CAMERA 0)
if(MR_STRESS_MODE STREQUAL "raw")
    set(MR_STRESS_FLUSH_MODE 7)
elseif(MR_STRESS_MODE STREQUAL "visible")
    set(MR_STRESS_FLUSH_MODE 0)
elseif(MR_STRESS_MODE STREQUAL "render")
    set(MR_STRESS_FLUSH_MODE 1)
elseif(MR_STRESS_MODE STREQUAL "fixedrender")
    set(MR_STRESS_FLUSH_MODE 1)
    set(MR_STRESS_FIXED_CAMERA 1)
elseif(MR_STRESS_MODE STREQUAL "everyN")
    set(MR_STRESS_FLUSH_MODE 2)
elseif(MR_STRESS_MODE STREQUAL "dirty")
    set(MR_STRESS_FLUSH_MODE 3)
elseif(MR_STRESS_MODE STREQUAL "dirtyfixed")
    set(MR_STRESS_FLUSH_MODE 3)
    set(MR_STRESS_FIXED_CAMERA 1)
elseif(MR_STRESS_MODE STREQUAL "lcdtest")
    set(MR_STRESS_FLUSH_MODE 4)
elseif(MR_STRESS_MODE STREQUAL "dirtyrect")
    set(MR_STRESS_FLUSH_MODE 5)
elseif(MR_STRESS_MODE STREQUAL "dirtyrectfixed")
    set(MR_STRESS_FLUSH_MODE 5)
    set(MR_STRESS_FIXED_CAMERA 1)
elseif(MR_STRESS_MODE STREQUAL "lace")
    set(MR_STRESS_FLUSH_MODE 6)
elseif(MR_STRESS_MODE STREQUAL "lacefixed")
    set(MR_STRESS_FLUSH_MODE 6)
    set(MR_STRESS_FIXED_CAMERA 1)
else()
    message(FATAL_ERROR
        "Unknown MR_STRESS_MODE '${MR_STRESS_MODE}'. Valid values: raw, visible, "
        "render, fixedrender, everyN, dirty, dirtyfixed, dirtyrect, "
        "dirtyrectfixed, lace, lacefixed, lcdtest")
endif()

# The dirty and lcdtest modes reserve a full-frame buffer, so a tall tile on top
# of that will not fit in RP2350 SRAM. Say so at configure time rather than as a
# link-time region overflow.
if(MR_APP STREQUAL "STRESS")
    if(MR_STRESS_FLUSH_MODE MATCHES "^(3|4)$" AND MR_TILE_H GREATER 32)
        message(WARNING
            "MR_STRESS_MODE=${MR_STRESS_MODE} reserves a full-frame buffer; "
            "MR_TILE_H=${MR_TILE_H} is likely to exhaust SRAM. Use MR_TILE_H=16.")
    endif()
endif()

if(MR_VIEW_H GREATER MR_SCREEN_H)
    message(FATAL_ERROR "MR_VIEW_H (${MR_VIEW_H}) cannot exceed MR_SCREEN_H (${MR_SCREEN_H})")
endif()

if(NOT MR_SCREEN_W EQUAL 320 OR NOT MR_SCREEN_H EQUAL 240)
    message(FATAL_ERROR
        "MicroRender platform builds are fixed at a 320x240 RGB565 logical target")
endif()

# The raw reference paths intentionally render one complete logical frame and
# only then begin the synchronous LCD transfer. A shorter tile would interleave
# drawing and presentation and would no longer represent clear/draw/send/loop.
if(MR_APP STREQUAL "GAME" AND MR_GAME_PRESENT_MODE EQUAL 0 AND
   NOT MR_TILE_H EQUAL MR_SCREEN_H)
    message(FATAL_ERROR
        "Raw game presentation requires MR_TILE_H=${MR_SCREEN_H}; got ${MR_TILE_H}")
endif()
if(MR_APP STREQUAL "STRESS" AND MR_STRESS_FLUSH_MODE EQUAL 7)
    if(NOT MR_TILE_H EQUAL MR_VIEW_H)
        message(FATAL_ERROR
            "Raw stress presentation requires MR_TILE_H=MR_VIEW_H (${MR_VIEW_H}); got ${MR_TILE_H}")
    endif()
    if(NOT MR_VIEW_H EQUAL MR_SCREEN_H)
        message(FATAL_ERROR
            "Raw stress presentation must send the complete ${MR_SCREEN_W}x${MR_SCREEN_H} frame")
    endif()
endif()

# ---------------------------------------------------------------------------
# shared sources and generated assets
# ---------------------------------------------------------------------------

set(MR_SHARED_DIR       "${CMAKE_CURRENT_LIST_DIR}/../shared")
set(MR_SHARED_SRC       "${MR_SHARED_DIR}/src")
set(MR_SHARED_RP2350    "${MR_SHARED_DIR}/rp2350")
set(MR_SHARED_TOOLS     "${MR_SHARED_DIR}/tools")
set(MR_SHARED_ASSETS    "${MR_SHARED_DIR}/assets")
set(MR_GENERATED_DIR    "${MR_SHARED_DIR}/generated")
set(MR_GENERATED_MRP    "${MR_GENERATED_DIR}/GAME.MRP")
set(MR_GENERATED_C      "${MR_GENERATED_DIR}/mr_embedded_assets.c")
set(MR_GENERATED_H      "${MR_GENERATED_DIR}/mr_embedded_assets.h")
set(MR_GENERATED_GAME_H "${MR_GENERATED_DIR}/game_mrp.h")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(MAKE_DIRECTORY "${MR_GENERATED_DIR}")

add_custom_command(
    OUTPUT "${MR_GENERATED_MRP}" "${MR_GENERATED_C}" "${MR_GENERATED_H}" "${MR_GENERATED_GAME_H}"
    COMMAND "${Python3_EXECUTABLE}" "${MR_SHARED_TOOLS}/mr_pack.py"
            --manifest "${MR_SHARED_ASSETS}/project.json"
            -o "${MR_GENERATED_MRP}"
            --header "${MR_GENERATED_GAME_H}"
            --symbol game_mrp
    COMMAND "${Python3_EXECUTABLE}" "${MR_SHARED_TOOLS}/mr_embed.py"
            --input "${MR_GENERATED_MRP}"
            --c "${MR_GENERATED_C}"
            --h "${MR_GENERATED_H}"
            --symbol mr_embedded_assets
    DEPENDS
            "${MR_SHARED_TOOLS}/mr_pack.py"
            "${MR_SHARED_TOOLS}/mr_embed.py"
            "${MR_SHARED_ASSETS}/project.json"
    WORKING_DIRECTORY "${MR_SHARED_DIR}"
    COMMENT "Generating shared MicroRender asset pack and Pico embedded assets"
    VERBATIM)

set(MR_SOURCES
    microrender.c
    "${MR_SHARED_SRC}/gfx.c"
    "${MR_SHARED_SRC}/gfx_font5x7.c"
    "${MR_SHARED_SRC}/gfx_triangle.c"
    "${MR_SHARED_SRC}/gfx_rgb444.c"
    "${MR_SHARED_SRC}/gfx_engine.c"
    "${MR_SHARED_SRC}/mr_strbuf.c"
    "${MR_SHARED_SRC}/mr_autodemo.c"
    "${MR_SHARED_RP2350}/mr_pico_ili9341.c"
    "${MR_SHARED_RP2350}/mr_pico_screenshot.c"
    "${MR_GENERATED_C}"
)

# Compile only the application actually selected, instead of building both and
# relying on --gc-sections to discard one.
if(MR_APP STREQUAL "STRESS")
    list(APPEND MR_SOURCES
        "${MR_SHARED_SRC}/mr_stress_test.c"
        "${MR_SHARED_RP2350}/mr_pico_stress_demo.c")
else()
    list(APPEND MR_SOURCES
        "${MR_SHARED_SRC}/mr_game_demo.c"
        "${MR_SHARED_RP2350}/mr_pico_demo.c")
endif()

# gfx_pack.c is the DOS disk-pack loader; Pico embeds assets instead. Include it
# only if the tree still carries it.
if(EXISTS "${MR_SHARED_SRC}/gfx_pack.c")
    list(APPEND MR_SOURCES "${MR_SHARED_SRC}/gfx_pack.c")
endif()

add_executable(microrender ${MR_SOURCES})

# USB CDC is enabled once for the whole firmware. Screenshot support is
# independent of verbose FPS logging, so every Pico application can answer the
# same SCREENSHOT command without enabling benchmark output.
set(MR_PICO_USB_STDIO OFF)
if(MR_PICO_SCREENSHOT OR
   (MR_APP STREQUAL "STRESS" AND MR_STRESS_PICO_SERIAL) OR
   (MR_APP STREQUAL "GAME" AND MR_PICO_GAME_SERIAL))
    set(MR_PICO_USB_STDIO ON)
endif()

target_include_directories(microrender PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${MR_SHARED_SRC}"
    "${MR_SHARED_RP2350}"
    "${MR_GENERATED_DIR}"
)

# ---------------------------------------------------------------------------
# compile definitions, assembled from the options above
# ---------------------------------------------------------------------------

target_compile_definitions(microrender PRIVATE
    GFX_COLOR_INDEX8=0
    GFX_PLATFORM_RP2350=1
    GFX_NO_LIBC_PIXEL_OPS=1

    MR_SCREEN_W=${MR_SCREEN_W}
    MR_SCREEN_H=${MR_SCREEN_H}
    MR_TILE_H=${MR_TILE_H}
    MR_VIEW_H=${MR_VIEW_H}

    MR_LCD_PIN_MISO=${MR_LCD_PIN_MISO}
    MR_LCD_PIN_CS=${MR_LCD_PIN_CS}
    MR_LCD_PIN_SCK=${MR_LCD_PIN_SCK}
    MR_LCD_PIN_MOSI=${MR_LCD_PIN_MOSI}
    MR_LCD_PIN_RST=${MR_LCD_PIN_RST}
    MR_LCD_PIN_DC=${MR_LCD_PIN_DC}
    MR_LCD_SPI_BAUD=${MR_LCD_SPI_BAUD}
    MR_ILI9341_MADCTL=${MR_ILI9341_MADCTL}
    MR_ILI9341_COLMOD_12BIT=$<BOOL:${MR_ILI9341_COLMOD_12BIT}>
    MR_STRESS_LACE_PHASES=${MR_STRESS_LACE_PHASES}
    MR_ILI9341_FRMCTR1_DIVA=${MR_ILI9341_FRMCTR1_DIVA}
    MR_ILI9341_FRMCTR1_RTNA=${MR_ILI9341_FRMCTR1_RTNA}

    MR_PICO_SYS_KHZ=${MR_PICO_SYS_KHZ}
    MR_PICO_PERI_PLL_KHZ=${MR_PICO_PERI_PLL_KHZ}
    MR_PICO_PERI_FROM_SYS=$<BOOL:${MR_PICO_PERI_FROM_SYS}>
    MR_PICO_CLOCK_REQUIRED=$<BOOL:${MR_PICO_CLOCK_REQUIRED}>
    MR_PICO_FRAME_PIPELINE=$<BOOL:${MR_PICO_FRAME_PIPELINE}>
    MR_PICO_PRESENT_CORE1=$<BOOL:${MR_PICO_PRESENT_CORE1}>
    MR_PICO_SCREENSHOT=$<BOOL:${MR_PICO_SCREENSHOT}>
    MR_PICO_USB_STDIO=$<BOOL:${MR_PICO_USB_STDIO}>
    MR_PICO_GAME_SERIAL=$<BOOL:${MR_PICO_GAME_SERIAL}>
    MR_GAME_PRESENT_MODE=${MR_GAME_PRESENT_MODE}
)

if(MR_APP STREQUAL "STRESS")
    target_compile_definitions(microrender PRIVATE
        MR_STRESS_TEST=1
        MR_STRESS_SPRITES=${MR_STRESS_SPRITES}
        MR_STRESS_TARGET_FPS=${MR_STRESS_TARGET_FPS}
        MR_STRESS_STATS_RATE=${MR_STRESS_STATS_RATE}
        MR_STRESS_HUD_MODE=${MR_STRESS_HUD_MODE}
        MR_STRESS_FIXED_CAMERA=${MR_STRESS_FIXED_CAMERA}
        MR_STRESS_PICO_FLUSH_MODE=${MR_STRESS_FLUSH_MODE}
        MR_STRESS_PICO_LCD_EVERY=${MR_STRESS_LCD_EVERY}
        MR_STRESS_PICO_DIRTY_MERGE_GAP=${MR_STRESS_DIRTY_MERGE_GAP}
        MR_STRESS_PICO_DIRTY_RECT_MAX_H=${MR_STRESS_DIRTY_RECT_MAX_H}
        MR_STRESS_PICO_DIRTY_FULL_THRESHOLD_PCT=${MR_STRESS_DIRTY_FULL_THRESHOLD_PCT}
        MR_STRESS_PICO_LACE_BLOCK_H=${MR_STRESS_LACE_BLOCK_H}
        MR_STRESS_PICO_WAIT_USB_MS=${MR_STRESS_PICO_WAIT_USB_MS}
        MR_STRESS_ENABLE_TRIANGLES=$<BOOL:${MR_STRESS_ENABLE_TRIANGLES}>
        MR_STRESS_CACHE_FIXED_BG=$<BOOL:${MR_STRESS_CACHE_FIXED_BG}>
        MR_STRESS_FAST_METRICS=$<BOOL:${MR_STRESS_FAST_METRICS}>
        MR_STRESS_PICO_DIAG=$<BOOL:${MR_STRESS_PICO_DIAG}>
        MR_STRESS_PICO_SERIAL=$<BOOL:${MR_STRESS_PICO_SERIAL}>
    )
else()
    target_compile_definitions(microrender PRIVATE
        MR_STRESS_TEST=0
        MR_STRESS_PICO_SERIAL=$<BOOL:${MR_STRESS_PICO_SERIAL}>
        MR_PICO_GAME_AUTORUN=1
        MR_PICO_AUTORUN_GAME_DEMO=1
    )
endif()

# ---------------------------------------------------------------------------
# codegen and link
# ---------------------------------------------------------------------------

target_compile_options(microrender PRIVATE
    -O3
    -fomit-frame-pointer
    -ffunction-sections
    -fdata-sections
)

target_link_options(microrender PRIVATE -Wl,--gc-sections)

# LTO is off by default for Pico SDK 2.2.0 with GCC 14.2 on Windows: with
# pico_stdlib linked and stdio disabled, -flto can make the SDK's wrapped
# puts/putchar/vprintf symbols disappear at final link. Turn it on only after
# confirming your SDK and toolchain combination links cleanly.
option(MR_PICO_ENABLE_LTO "Enable LTO for MicroRender Pico builds" OFF)
if(MR_PICO_ENABLE_LTO)
    target_compile_options(microrender PRIVATE -flto=auto)
    target_link_options(microrender PRIVATE -flto=auto)
endif()

target_link_libraries(microrender
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_timer
    hardware_clocks
)

if(MR_PICO_PRESENT_CORE1)
    target_link_libraries(microrender pico_multicore)
    if(NOT MR_STRESS_MODE MATCHES "^lace")
        message(WARNING
            "MR_PICO_PRESENT_CORE1 only affects lace presentation; "
            "MR_STRESS_MODE=${MR_STRESS_MODE} ignores it.")
    endif()
    if(MR_TILE_H LESS MR_VIEW_H)
        message(WARNING
            "MR_PICO_PRESENT_CORE1 needs a full-height tile "
            "(MR_TILE_H=${MR_TILE_H}, MR_VIEW_H=${MR_VIEW_H}); ignored.")
    endif()
endif()

if(MR_PICO_USB_STDIO)
    pico_enable_stdio_usb(microrender 1)
else()
    pico_enable_stdio_usb(microrender 0)
endif()
pico_enable_stdio_uart(microrender 0)
pico_add_extra_outputs(microrender)

# ---------------------------------------------------------------------------

message(STATUS "MicroRender: app=${MR_APP} board=${PICO_BOARD}")
message(STATUS "MicroRender: USB screenshot=${MR_PICO_SCREENSHOT} USB stdio=${MR_PICO_USB_STDIO}")
if(MR_APP STREQUAL "STRESS")
    message(STATUS "MicroRender: mode=${MR_STRESS_MODE} (flush=${MR_STRESS_FLUSH_MODE}, "
                   "fixed_camera=${MR_STRESS_FIXED_CAMERA}) sprites=${MR_STRESS_SPRITES}")
else()
    message(STATUS "MicroRender: game presentation=${MR_GAME_PRESENTATION}")
endif()
message(STATUS "MicroRender: ${MR_SCREEN_W}x${MR_SCREEN_H} tile_h=${MR_TILE_H} "
               "view_h=${MR_VIEW_H} sys=${MR_PICO_SYS_KHZ}kHz spi=${MR_LCD_SPI_BAUD}Hz")
