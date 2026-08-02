# MicroRender

[![CI](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml/badge.svg)](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A portable software renderer that runs the same C99 core on a Raspberry Pi
Pico 2 and on a 16-bit DOS machine.

**The renderer never allocates a framebuffer.** It rasterizes into a
caller-provided tile and hands finished scanlines to a flush callback. That one
constraint is what lets a 320x200 scene fit under a single 64 KiB segment on
DOS *and* fit in RP2350 SRAM alongside a DMA transfer already in flight. Two
very different machines, one reason.

Everything else follows from it: the tile is the unit of work, so the flush
callback can be split into `begin`/`wait` and the renderer can rasterize tile
N+1 while tile N is still going out over SPI; and because the core owns no
memory, it can be compiled for the host and unit tested like ordinary code.

![Pico2](https://raw.githubusercontent.com/SaxonRah/microrender/main/microrender/pico2_screenshot.png)

*Captured over USB by `tools_capture_pico_screenshot.py`, which reads framebuffer
data back from the Pico and reconstructs the image on the host.*

---

## Measured

The row-start index on RLE sprites is the single largest win in the renderer.
Host benchmark, 32x32 sprite with a 22% transparent border, 512 sprites/frame
into a 320x240 RGB565 target with 16-row tiles:

| blit path | fps | vs. colorkey |
| --- | ---: | ---: |
| raw opaque (`memcpy` per row) | 5581 | 2.53x |
| colorkey, per-pixel test | 2205 | 1.00x |
| RLE, linear run scan | 1009 | **0.46x** |
| RLE + row-start index | 4625 | **2.10x** |

The interesting row is the third one. Naive RLE is *slower than testing every
pixel*, because each tile re-walks all 32 runs in the sprite looking for the
ones on its rows. The row-start index turns that scan into two array lookups
and is what makes RLE worth having at all.

Tile height matters almost as much, and this is where DOS pays for its segment
limit:

| tile height | bytes/tile | fps |
| ---: | ---: | ---: |
| 4 rows | 2,560 | 1939 |
| 16 rows | 10,240 | 4690 |
| 60 rows | 38,400 | 7834 |
| 240 rows (full frame) | 153,600 | 10476 |

16 rows is the largest tile that fits comfortably in one 64 KiB DOS segment
alongside everything else, and it costs about 55% of the throughput available
to the Pico, which can afford the whole frame in one buffer.

Reproduce both tables with `cmake --workflow` or:

```sh
cmake -S tests -B build/bench -DCMAKE_BUILD_TYPE=Release
cmake --build build/bench
./build/bench/mr_test_bench
```

These are host measurements of the renderer in isolation. They are useful for
comparing renderer configurations against each other and are **not** a
prediction of DOS or Pico framerate.

---

## Correctness

The shared core compiles clean under `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wundef
-Werror`, allocates nothing, and uses no recursion or VLAs.

CI runs on Linux, macOS and Windows, in both pixel formats, with
AddressSanitizer and UndefinedBehaviorSanitizer and `-fno-sanitize-recover`:

- **Unit tests.** The most important one renders the same sprite through all
  four blit paths at 132 positions — every screen edge, both sides of every
  tile seam, fully offscreen — and compares the framebuffers byte for byte.
  Four implementations of one specification, so any disagreement localises the
  bug immediately.
- **Fuzz harness.** Every drawing entry point is called with coordinates from
  -4000 to +4000, through randomised clip windows, sub-region passes and the
  pipelined double-buffered path. The flush callback independently re-validates
  every rect the renderer hands it.

```sh
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

No Pico SDK, Open Watcom or DOSBox required for any of that. See
[tests/README.md](tests/README.md).

---

## How the core is portable

Three independent axes, each isolated in one header, none of them leaking into
renderer or game code:

| axis | header | DOS | Pico 2 |
| --- | --- | --- | --- |
| pixel format | `gfx_color.h` | `uint8_t` RGB332 palette index | `uint16_t` RGB565 |
| pointer model | `gfx_config.h` | far pointers (`-ml`) | flat |
| fixed point | `gfx_fixed.h` | 8.8, 32-bit intermediates | 16.16, 64-bit intermediates |

`GFX_RGB565(r,g,b)` transparently becomes a palette index in the DOS build, so
demo and game code compiles unchanged against either. Switching the whole core
to the DOS pixel format on the host is one CMake flag
(`-DMR_TESTS_INDEX8=ON`), and CI tests both.

### Presentation is a callback

```c
void gfx_init(gfx_renderer_t *r, int w, int h,
              gfx_color_t *tile_buffer, int tile_h,
              gfx_flush_fn flush, void *user);
```

DOS supplies a flush that `_fmemcpy`s into `A000:0000`. The Pico supplies one
that starts an SPI DMA. Where a target can overlap transfer with rasterization,
it installs `flush_begin`/`flush_wait` instead and calls
`gfx_render_tiled_pipelined()` with a second tile buffer; targets that cannot
simply omit them and the same call degrades to the synchronous path.

---

## Repository layout

```text
shared/
  src/          portable renderer, game/actor layer, asset format
  rp2350/       RP2350 support code (ILI9341 driver, demos)
  assets/       source art, maps, audio
  tools/        asset pipeline (Python) and converters
  generated/    generated pack + embedded C assets (do not hand-edit)
microrender/       Pico 2 frontend and CMake build
microrender_dos/   DOS frontend and Open Watcom build
tests/             host test target: unit, fuzz, benchmark
```

Platform directories stay thin. New renderer features go in `shared/src`; new
art goes in `shared/assets`.

---

## Asset pipeline

One source tree, one compiled pack, both targets.

```text
shared/assets/  --mr_pack.py-->  GAME.MRP  --mr_embed.py-->  mr_embedded_assets.c
                                    |                              |
                                 DOS reads                    Pico links
                                 from disk                    into firmware
```

`.MRP` ("MicroRenderPackage") is a little-endian pack with a 12-byte header and
44-byte directory entries, holding sprites (raw and RLE), tilemaps, palettes,
animations, collision and tile flags, spawns, triggers and audio. The build
prints a SHA-256 of the pack so stale generated files are easy to spot.

Because DOS reads runs straight off disk, anything decoded from a `.MRP` should
be passed through `gfx_sprite_rle_validate()` once at load. The hot blit paths
deliberately do not re-check runs per frame.

```bat
mr build assets
```

---

## Building

### Host tests and benchmark

Needs only CMake and a C99 compiler. See above.

### Pico 2 / RP2350

Every build variant is a CMake option. Nothing is configured by rewriting
source files.

```sh
cmake -S microrender -B microrender/build -DMR_APP=GAME
cmake --build microrender/build
# -> microrender/build/microrender.uf2, copy to the Pico in BOOTSEL mode
```

Presets cover the common variants:

```sh
cmake --preset stress-lace          # 1024 sprites, temporal row groups
cmake --preset stress-render        # renderer-only, serial FPS reporting
cmake --preset stress-dirtyrect     # coalesced dirty-rectangle flush
```

Key options: `MR_APP` (`GAME`/`STRESS`), `MR_STRESS_MODE`, `MR_STRESS_SPRITES`,
`MR_TILE_H`, `MR_PICO_SYS_KHZ`, `MR_LCD_SPI_BAUD`, `MR_PICO_FRAME_PIPELINE`.
Run `cmake -LH -S microrender -B microrender/build` for the full annotated list.

Requires the Pico SDK 2.2.0, ARM GCC, Ninja and CMake — all installed by the
official Raspberry Pi Pico VS Code extension.

### 16-bit DOS

Requires Open Watcom with `WATCOM` set.

```bat
mr build dos
```

Output: `microrender_dos/dist/mrender.exe`. The DOS build compiles the shared
renderer from `..\shared\src` and links it against the frontend in
`microrender_dos/dos`, which owns only mode 13h setup, the RGB332 palette, the
INT 9 keyboard handler, and the flush to `A000`.

### Everything

```bat
mr build all
```

---

## Known limits

- **Sprite size on DOS.** Row addressing inside the blitters is `int` math. On
  a 16-bit `int` target that overflows above 32767 pixels, so
  `gfx_sprite_ready()` refuses to draw a sprite larger than that rather than
  wrap silently. 32-bit targets compile the check away.
- **RLE pixel pools on DOS.** Far-pointer arithmetic wraps within a segment, so
  a single RLE pixel pool must stay under 64 KiB.
- **Clip is per-tile state.** `gfx_begin_tile_rect()` resets it at the top of
  every tile, so a clip must be set inside the `draw_scene` callback, not
  before `gfx_render_tiled()`.
- **DOSBox framerates are not hardware framerates.** The capture scripts run
  with `cycles=max`. Treat those numbers as relative comparisons between
  renderer configurations, not as a claim about period hardware.

---

## Possible future targets

The shared layout means a new target needs a flush callback and a main loop,
not another copy of the renderer: Win32, Linux framebuffer, SDL, other
microcontrollers, other retro PCs.

---

## License

MIT. See [LICENSE](LICENSE).
