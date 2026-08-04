# Pico 2 presentation modes

Pico game and stress builds use a fixed 320×240 RGB565 logical render target.
The frontend may allocate a full-frame working buffer or smaller tiles depending
on the selected presentation path. The shared game/stress HUD renders current and
average FPS.

## Game modes

| `MR_GAME_PRESENTATION` | behavior | buffers |
| --- | --- | --- |
| `raw` | render the complete frame, synchronously send it, then loop | one full frame |
| `pipelined` | overlap LCD DMA for frame N with rendering frame N+1 | two working buffers |

```powershell
.\mr.bat build pico game
.\mr.bat build pico game-raw
.\mr.bat build pico game presentation=pipelined tile=240 sys=300000 spi=75000000
```

Raw game mode requires `MR_TILE_H=240`. It intentionally omits the second
pipeline buffer and remains a serialized reference path.

## Stress modes

| mode | what it measures or presents | visible consequence |
| --- | --- | --- |
| `raw` | complete render followed by synchronous full-frame upload | none; deliberately serialized |
| `visible` | optimized RGB565 presentation every rendered frame | normal full update |
| `lace` | fully render each frame, send alternating row groups | possible temporal shimmer in motion |
| `dirtyrect` | compare against retained frame and send changed rectangles | scene-dependent gain |
| `everyN` | send a full update every Nth rendered frame | visible presentation stutter |
| `render` | show one proof frame, then rasterize with null LCD flushes | LCD stops changing; serial is measurement source |
| `fixedrender` | render-only path with fixed camera | static-background isolation |
| `dirty` | older retained full-frame dirty comparison | scene-dependent gain |
| `dirtyfixed` | `dirty` with fixed camera | static-background isolation |
| `dirtyrectfixed` | `dirtyrect` with fixed camera | static-background isolation |
| `lacefixed` | `lace` with fixed camera | static-background isolation |
| `lcdtest` | dedicated LCD diagnostic pattern | diagnostic only |

### Friendly names and numeric flush modes

| name | numeric flush mode | fixed camera |
| --- | ---: | ---: |
| `visible` | 0 | no |
| `render` | 1 | no |
| `fixedrender` | 1 | yes |
| `everyN` | 2 | no |
| `dirty` | 3 | no |
| `dirtyfixed` | 3 | yes |
| `lcdtest` | 4 | no |
| `dirtyrect` | 5 | no |
| `dirtyrectfixed` | 5 | yes |
| `lace` | 6 | no |
| `lacefixed` | 6 | yes |
| `raw` | 7 | no |

## Presets

```powershell
.\mr.bat build pico stress-raw
.\mr.bat build pico stress-visible
.\mr.bat build pico stress-lace sprites=1024 lace=4 sys=300000 spi=75000000
.\mr.bat build pico stress-render sprites=1024 serial=ON
.\mr.bat build pico stress-dirtyrect sprites=512 tile=16
.\mr.bat build pico all
```

`pico all` builds `game`, `game-raw`, `stress-visible`, `stress-raw`,
`stress-lace`, `stress-render`, and `stress-dirtyrect` with Ninja and Pico ARM
GCC. The driver removes caches with a mismatched generator/compiler or stale
absolute source/build paths.

Raw stress mode requires `MR_TILE_H=MR_VIEW_H` and a complete 240-row view.
`dirty`, `dirtyfixed`, and `lcdtest` retain a full frame; CMake warns when those
modes are combined with tiles taller than 32 rows because RP2350 SRAM can be
exhausted.

## Known hardware measurements

For the previously measured 1,024-sprite scene at a requested 300 MHz system
clock and a measured 75 MHz SPI clock:

| mode | observed result |
| --- | ---: |
| `visible` | about 55.8 FPS |
| `lace` | about 76–77 FPS |

These are measurements of specific firmware/hardware settings, not universal
performance guarantees. The checked-in `stress-visible` preset currently uses
512 sprites; `stress-lace` uses 1,024. Dirty-rectangle performance is
scene-dependent and the preset uses 512 sprites with 16-row tiles.

## Universal USB screenshots

`MR_PICO_SCREENSHOT` defaults to `ON`. CMake enables USB stdio when screenshot
support is enabled, even if verbose game/stress serial logging is disabled.
Therefore screenshots work with every current game, stress, and LCD-test build
without `serial=ON`.

Install host dependencies:

```powershell
py -m pip install pyserial pillow
```

Build and flash any preset, close any serial monitor holding the Pico's CDC port,
then run:

```powershell
py -u .\microrender\tools_capture_pico_screenshot.py COM5 .\pico2_screenshot.png --timeout 30
```

The firmware accepts:

```text
SCREENSHOT
SHOT
PING
HELP
```

`SCREENSHOT` and `SHOT` return:

```text
MRSHOT1 <width> <height> <byte_count>
<raw little-endian RGB565 bytes>
```

The service polls non-blockingly once per frame. Before reusing the working
buffer, it waits for active LCD DMA. It then creates a temporary renderer whose
flush callback streams each complete-width tile over USB. No separate screenshot
framebuffer is allocated.

A screenshot always represents a newly rendered complete logical frame:

- raw/full-frame modes reuse their full-frame working buffer
- tiled modes rerender and stream tile by tile
- lace captures the clean full frame rather than the LCD's temporary alternating
  row-group state
- render-only captures the current simulation even though the LCD remains on its
  proof frame
- `lcdtest` captures the generated diagnostic scene

To disable the service and USB stdio when no other serial feature is enabled:

```powershell
.\mr.bat build pico game MR_PICO_SCREENSHOT=OFF
```

## Serial metrics versus screenshots

`serial=ON` sets both `MR_STRESS_PICO_SERIAL` and `MR_PICO_GAME_SERIAL`. It enables
verbose startup/FPS text and adds a short boot delay so a terminal can see the
banner. It is independent of `MR_PICO_SCREENSHOT`.

Render-only mode still needs `serial=ON` when USB text is the desired performance
measurement source, even though screenshots themselves do not require it.
