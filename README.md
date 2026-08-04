# MicroRender

[![CI](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml/badge.svg)](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

MicroRender is a small C99 software renderer and shared game demo targeting:

- Raspberry Pi Pico 2 / RP2350 with an ILI9341 display
- 16-bit DOS with Open Watcom and VGA Mode X
- desktop Windows, Linux, and macOS through Raylib
- native host tests and benchmarks

Every shipping frontend uses the same **320×240 RGB565 logical framebuffer**,
the same renderer core, and the same game simulation. Platform code owns only
input, timing, and presentation.

> Standard VGA is palettized rather than true-colour. The DOS build still
> renders RGB565 everywhere in shared code, then converts to a fixed RGB332
> palette only while presenting to 320×240 Mode X. Pico and Raylib present
> RGB565 directly.

![Pico2](https://raw.githubusercontent.com/SaxonRah/microrender/main/microrender/pico2_screenshot.png)

---

## One command-line build entry point

Run everything from the repository root through `mr.bat`:

```bat
.\mr.bat help
.\mr.bat build tests
.\mr.bat build dos mode=both tile=16 vsync=0
.\mr.bat build pico game presentation=pipelined tile=240 sys=300000 spi=75000000
.\mr.bat build pico game-raw
.\mr.bat build raylib demo=game mode=tiled tile=16 scale=3 fps=0
```

Build variants are arguments, not source edits. See [Building.md](Building.md)
and [SCRIPTS.md](SCRIPTS.md).

---

## Presentation paths

The deliberately slow baseline is kept beside the optimized paths so the cost
of each optimization can be measured rather than inferred.

### Raw reference

```text
clear complete 320×240 frame
draw everything
send/upload complete frame
begin next loop
```

No drawing overlaps presentation. On Pico it uses one full-frame RGB565 buffer
and a synchronous LCD transfer. On DOS it draws tiles into a 150 KiB Open
Watcom huge-memory RGB565 framebuffer, then converts and uploads the complete
frame. On Raylib it draws the complete image, then performs one full RGB565
texture upload.

### Optimized paths

- **Pipelined Pico game:** DMA transmits frame N while the CPU draws frame N+1.
- **Tiled DOS/Raylib:** completed strips are presented immediately rather than
  staging another complete frame.
- **Temporal row groups (`lace`):** fully render each frame but present
  alternating row groups, reducing the transmitted payload per presentation.
- **Dirty rectangles:** update only changed regions; useful when much of the
  scene remains still.
- **Render-only stress mode:** measure rasterization without continuous display
  transfer.

Every game and stress frontend includes current and average FPS in its rendered
HUD. Pico can additionally print metrics over USB serial, and DOS stress prints
its final summary after returning to text mode.

---

## Shared game demo

The same game state runs on Pico, DOS, Raylib, and in host tests:

- 1024×1024 tilemap world viewed through a bounded 320×240 camera
- tile collision and collider-aware stage bounds
- moving enemies
- collectible pickups
- deterministic tiny RGB565 pixel particles on pickups and enemy collisions
- enemy contact restarts the demo, resets player and camera, clears every
  pickup and trigger, and increments the restart counter
- keyboard/manual control or deterministic autoplay

The gameplay test protects camera clamping, actor world bounds, pickup
collection, particle creation, enemy restart behavior, pickup clearing, and the
fixed three-pixel player step that previously moved twice per tick.

---

## Renderer architecture

The renderer never allocates its own framebuffer. The caller supplies a tile or
full-frame working buffer and a flush callback:

```c
void gfx_init(gfx_renderer_t *r, int w, int h,
              gfx_color_t *tile_buffer, int tile_h,
              gfx_flush_fn flush, void *user);
```

That keeps the core portable across flat-memory systems and 16-bit segmented
DOS. A target that can overlap transfer with rendering supplies asynchronous
`begin`/`wait` callbacks and calls `gfx_render_tiled_pipelined()` with a second
buffer. Other targets use the synchronous tiled path.

All shipping targets compile the core as RGB565. Pointer and fixed-point
choices remain isolated in platform configuration:

| concern | DOS | Pico / Raylib / host |
| --- | --- | --- |
| logical resolution | 320×240 | 320×240 |
| renderer colour | RGB565 | RGB565 |
| physical presentation | Mode X, fixed RGB332 DAC palette | RGB565 |
| pointer model | Open Watcom large/far/huge pointers | flat pointers |
| normal game buffer | 16-row RGB565 tile | full frame on Pico; configurable on Raylib |
| raw reference buffer | 150 KiB huge RGB565 frame | full RGB565 frame |

---

## RLE and host benchmark

MicroRender's RLE representation stores horizontal spans of opaque pixels. The
visible colours remain verbatim; transparent pixels are omitted, and a
row-start index lets each tile jump directly to the runs on its rows.

One recorded development-machine run, 32×32 circular sprite, 22% transparent,
512 sprites per frame, 320×240 RGB565, 16-row tiles:

| blit path | FPS |
| --- | ---: |
| raw opaque row copy | 5,581 |
| per-pixel colorkey | 2,205 |
| linear RLE scan | 1,009 |
| RLE with row-start index | 4,625 |

Linear-scan RLE varies substantially across compilers and machines because each
tile repeatedly re-walks the sprite's run list. The indexed path remained the
fastest transparency-preserving path in all recorded runs and was the least
variable measured path.

Reproduce the current host result with:

```bat
.\mr.bat bench 200
```

These are renderer-isolation host measurements, not Pico or DOS framerates.

---

## Correctness

```bat
.\mr.bat build tests
.\mr.bat test
```

The RGB565 host suite includes:

- byte-for-byte equivalence of colorkey, linear RLE, and indexed RLE over 132
  clipped and tile-seam positions
- raw opaque alignment tests
- clipping, dirty-region, RLE validation, tile-capacity, and collision tests
- deterministic game behavior tests
- four deterministic fuzz seeds covering every drawing entry point and the
  pipelined path
- a headless compile/run check for every Raylib presentation mode

Linux and macOS CI run ASan+UBSan with warnings as errors. Windows is pinned to
VS2022 for mandatory MSVC unit, game, and fuzz coverage. A separate headless job
builds and executes the Raylib raw, tiled, lace, dirty-rectangle, game, and
stress paths. See
[tests/README.md](tests/README.md).

---

## Repository layout

```text
shared/
  src/                  renderer, actors/camera/collision, game and stress demos
  rp2350/               Pico ILI9341 and demo frontends
  assets/               source assets and manifest
  tools/                pack and embedding tools
microrender/             Pico SDK project and presets
microrender_dos/         Open Watcom / Mode X frontend
microrender_raylib/      desktop RGB565 frontend
scripts/                 unified build, run, tool-location, and clean drivers
tests/                   unit, game, fuzz, and benchmark targets
```

---

## Quick start

### Raylib desktop

```bat
.\mr.bat build raylib raylib=C:\path\to\raylib demo=game mode=tiled tile=16 scale=3
.\mr.bat run raylib --demo game --mode tiled --tile 16
.\mr.bat run raylib --demo game --mode raw --autoplay
.\mr.bat run raylib --demo stress --mode lace --sprites 1024 --lace-block 4
```

### Pico 2

```bat
.\mr.bat build pico game
.\mr.bat build pico game-raw
.\mr.bat build pico stress-visible
.\mr.bat build pico stress-raw
.\mr.bat build pico stress-lace sprites=1024 sys=300000 spi=75000000
.\mr.bat build pico all

.\mr.bat build pico game vscode
.\mr.bat build pico game-raw vscode
.\mr.bat build pico stress-visible vscode
.\mr.bat build pico stress-raw vscode
.\mr.bat build pico stress-lace vscode
.\mr.bat build pico stress-render vscode
.\mr.bat build pico stress-dirtyrect vscode
```

Pico command-line builds are forced through Ninja and the Pico ARM GCC
toolchain. The driver also detects and removes stale per-preset caches created
with Visual Studio/MSVC, so the same commands work from PowerShell and VS Code.

Adding `vscode` will force the selected build to be placed in `microrender\build` so that vscode can upload that specific build to the pico instead of placing it in `microrender\build-stress-dirtyrect`, `microrender\build-stress-lace`, `microrender\build-stress-raw`, `microrender\build-stress-render`, `microrender\build-stress-visible`, `microrender\build-game-raw`.

### DOS

```bat
.\mr.bat build dos both tile=16 vsync=0
.\mr.bat run dos /auto
.\mr.bat run dosraw /auto
.\mr.bat run stress 512 2100
.\mr.bat run stressraw 512 2100
```

DOSBox `cycles=max` measures the host, not a period machine. Set
`MR_DOSBOX_CYCLES` before quoting DOS performance.

---

## Known constraints

- A stock VGA DAC cannot display RGB565 directly; DOS quantizes at presentation.
- A DOS raw frame requires a 150 KiB huge allocation. The optimized DOS path
  avoids that full-frame staging allocation.
- Individual DOS RLE pixel pools must remain below a 64 KiB segment.
- Pico raw game/stress modes require a 240-row tile by design so they remain a
  true clear/draw-complete-frame/send baseline.
- Dirty-rectangle gains are scene-dependent.

---

## License

MIT. See [LICENSE](LICENSE).
