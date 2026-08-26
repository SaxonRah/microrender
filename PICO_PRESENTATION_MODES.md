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

### Measured on a Pimoroni Pico Plus 2 (RP2350)

All figures from `stress-lace` with `MR_PICO_PRESENT_CORE1=ON`, 1024 sprites,
75 MHz SPI, 300 MHz system. Wire rate is `sentKB` divided by `frameUs`.

| configuration | KiB/frame | frameUs | FPS | wire rate | render |
| --- | ---: | ---: | ---: | ---: | --- |
| 16bpp, block_h=4 | 75.0 | 9072 | 110.0 | 68.1 Mb/s (90.9%) | clean |
| 16bpp, block_h=8 | 75.0 | 9017 | 110.8 | 68.1 Mb/s (90.9%) | clean |
| 12bpp, half frame | 56.25 | 8871 | 112.6 | 51.9 Mb/s (69.3%) | scrambled |
| 12bpp, full frame | 112.5 | 17477 | 57.1 | 52.7 Mb/s (70.3%) | clean |

`cpuUs` sits at 5.0-5.4 ms in every one of these, so rasterization is about
5.2 ms and would sustain roughly 190 FPS on its own. `flushUs` is core 0's
*wait*, not the transfer time: the transfer spans the whole frame, overlapping
the render and blocking core 0 for the remainder.

Three things fall out of this, two of which contradict what the earlier drafts
of this document claimed.

**Per-block overhead is small.** Doubling the row group from 4 to 8 rows halves
the number of window setups and saved 55 us, so a block costs about 3.7 us.
The ~880 us gap between 8.19 ms of theoretical wire time and the 9.07 ms
actually observed is therefore not window setup -- it is PL022 framing. At
90.9% of the SPI clock there is very little left to reclaim by restructuring
the transfer.

**12 bpp does not pay on this transport.** Sending 25% fewer bytes produced
1.6% more FPS. Both 16bpp configurations reach 68.1 Mb/s and both 12bpp ones
about 52 Mb/s: 8-bit DMA frames are roughly 70% wire-efficient against 90.9%
for 16-bit, and the framing overhead consumes the entire byte saving. The
full-frame 12bpp case is the clearest statement of it -- 57.1 FPS, no better
than the 55.8 FPS the plain 16bpp `visible` mode already managed.

**The panel is the ceiling, not the bus.** `MR_ILI9341_FRMCTR1_RTNA=0x10`, the
datasheet's 119 Hz setting, produced a washed-out white screen with the image
faint behind it -- fewer clocks per line means less time to charge each row, so
the crystal never fully switches. Sweep `0x19` (76 Hz), `0x18` (79 Hz),
`0x16` (86 Hz) and stop at the last value with acceptable contrast. Until it is
raised, presenting above ~70 FPS produces frames the panel overwrites before
scanning out, which is the real reason there is nothing left to win here.

**Recommended configuration:** the `stress-lace` preset, which now enables
`MR_PICO_PRESENT_CORE1` by default: 110.8 FPS at stock clocks, stock pixel
format, and a clean image. It costs a second 150 KiB frame buffer.

### Panel refresh sweep: measured, and it changes nothing useful

The board presents faster than the panel scans. At the reset default the panel
runs near 70 Hz, so roughly 40 of every 110 frames are overwritten in GRAM
before they are displayed. Raising `FRMCTR1` looked like the last remaining
change that would alter what is actually visible.

Swept on a Pimoroni Pico Plus 2 with a generic ILI9341, `stress-lace` with the
core-1 presenter, judged by eye against the `0x1B` build:

| RTNA | nominal | result |
| --- | --- | --- |
| `0x1B` | 70 Hz | reset default, clean |
| `0x19` | 76 Hz | clean, no visible difference |
| `0x18` | 79 Hz | clean, no visible difference |
| `0x16` | 86 Hz | clean, no visible difference |
| `0x13` | 100 Hz | noticeably brighter and washed out |
| `0x10` | 119 Hz | washed to near-white, image faint behind it |

Everything up to 86 Hz is indistinguishable from the default and everything past
it is worse, so the default stays at `0x1B`. `MR_ILI9341_FRMCTR1_RTNA` remains
available for panels that behave differently, and `scripts\mr_frmctr_sweep.bat`
builds the whole ladder, but on this hardware there is nothing to gain.

The washout is what too few clocks per line does to row charge time: less time
to charge each row means the crystal never fully switches. The datasheet
maximum is not what a given module will hold, and the usable limit is per
module and per wiring.

This is worth stating plainly because it closes the question the rest of this
document opens. Presenting above the panel's scan rate produces frames that are
overwritten before they are shown, and raising the scan rate to catch up is not
available here. So 110 FPS is where this hardware ends: not because the bus
cannot go faster, but because nothing downstream can show the result.

### Build directories are per preset, not per flag

`mr.bat build pico <preset> MR_FOO=ON` puts the build in the preset's directory.
Passing different `MR_*` overrides to the same preset reuses that directory, and
CMake cache variables persist: a later command that simply omits `MR_FOO` does
not clear it. The build then silently does not match the command that produced
it.

`pico_prepare_build_dir` now stamps the flags into `.mr_build_flags` and wipes
the directory when they change, so this is handled automatically. If a build
behaves unexpectedly, check `sentKB` in the serial output against what the
configuration should send -- at 320x240 that is 150 KiB for a full 16bpp frame,
75 KiB for a 16bpp half frame, 112.5 KiB for a full 12bpp frame and 56.25 KiB
for a 12bpp half frame. It is the quickest way to confirm what actually got
built.

### Split-PLL SPI clock: does not work, presets removed

`MR_PICO_PERI_PLL_KHZ` exists to work around the SPI baud generator, which
divides `clk_peri` by `prescale * postdiv`: from 300 MHz the reachable rates
step 150 / 75 / 50 / 37.5, so anything requested between 75 and 150 lands back
on 75. Reaching 85 MHz while keeping `clk_sys` at 300 needs a 340 MHz PLL.

Two presets that did this were added and then removed. Both hang before the
first serial print. The original cause was clear -- the function passed the PLL
rate to `set_sys_clock_khz()`, running the whole chip including XIP flash at
340 MHz while executing the clock-change code from that flash -- but rewriting
it to park `clk_sys` on `clk_ref`, retune `pll_sys`, and return at 300 MHz did
not fix it. The path remains unusable and is not currently worth chasing:
the panel will not display much past ~86 Hz, and lace already reaches 110 FPS
at 75 MHz, so faster SPI has nothing to buy.

### Build directories are per preset, not per flag

`mr.bat build pico <preset> MR_FOO=ON` puts the build in the preset's directory.
Passing different `MR_*` overrides to the same preset reuses that directory, and
CMake cache variables persist: a later command that simply omits `MR_FOO` does
not clear it. The build then silently does not match the command that produced
it.

`pico_prepare_build_dir` now stamps the flags into `.mr_build_flags` and wipes
the directory when they change, so this is handled automatically. If a build
behaves unexpectedly, check `sentKB` in the serial output against what the
configuration should send -- at 320x240 that is 150 KiB for a full 16bpp frame,
75 KiB for a 16bpp half frame, 112.5 KiB for a full 12bpp frame and 56.25 KiB
for a 12bpp half frame. It is the quickest way to confirm what actually got
built.

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
