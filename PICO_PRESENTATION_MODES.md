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

### Why those two numbers look the way they do

A 320x240 RGB565 frame is 1,228,800 bits. The SPI clock therefore sets a hard
ceiling that no amount of rasterizer work can cross:

| SPI clock | full frame | ceiling | half frame (`lace`) | ceiling |
| ---: | ---: | ---: | ---: | ---: |
| 75 MHz | 16.38 ms | 61.0 FPS | 8.19 ms | 122.1 FPS |
| 85 MHz | 14.46 ms | 69.2 FPS | 7.23 ms | 138.4 FPS |

`visible` at 55.8 FPS is running at about 91% of the 75 MHz full-frame ceiling.
It is a bus measurement, not a renderer measurement. `lace` exceeds 61 FPS only
because it sends half the rows per presentation; its *complete-image* rate is
about 38 Hz.

Subtracting wire time from the measured `lace` frame time leaves roughly 5 ms of
rasterization, so the renderer alone would sustain something near 200 FPS. On
this hardware the renderer is not the limit and has not been for some time.

Two consequences worth keeping in mind before chasing a bigger number:

- `lace` presents with the blocking `mr_pico_ili9341_flush`, so its ~5 ms of
  rasterization and ~8 ms of transfer are strictly serial even though
  `tile_buffer_b` is already allocated.
- The panel scans its own GRAM at whatever `FRMCTR1` says, which this driver
  never writes; the reset default is about 70 Hz. Above that, extra
  presentations are overwritten before they are scanned out. The ILI9341 TE pin
  is likewise unused, so nothing is synchronized to the panel scan.

### Split-PLL SPI clock

The RP2350 SPI baud generator divides `clk_peri` by `prescale * postdiv`. With
`clk_peri` at 300 MHz the reachable rates step 150 / 75 / 50 / 37.5 MHz, so a
request anywhere between 75 and 150 MHz lands back on 75. 75 MHz is a divider
artifact, not a panel limit.

`MR_PICO_PERI_PLL_KHZ` already exists to work around this: it runs the system
PLL at a higher rate, attaches `clk_peri` directly to it, and divides `clk_sys`
back down separately. Two presets now exercise it:

```powershell
.\mr.bat build pico stress-lace-85
.\mr.bat build pico stress-visible-85
```

Both set the system PLL to 340 MHz, `clk_sys` to the known-good 300 MHz, and
the SPI clock to an exact 340/4 = 85 MHz. Confirm the achieved rate from the
`spi: requested=... actual=...` line rather than assuming it took.

85 MHz is above the ILI9341 datasheet write-cycle rating and is a per-board
question: it depends on wire length, level shifting, and the specific panel.
If the display shows dropped or smeared pixels, step back to 75 MHz. This is
why the faster values are separate presets rather than new defaults.

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
