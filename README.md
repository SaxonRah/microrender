# MicroRender

[![CI](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml/badge.svg)](https://github.com/SaxonRah/microrender/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

MicroRender is a compact multiplatform software renderer first, with optional
shared scene helpers plus game and stress-demo code for:

- Raspberry Pi Pico 2 / RP2350 with an ILI9341 display
- 16-bit DOS with Open Watcom and VGA Mode X
- Windows, Linux, and macOS through Raylib
- native host tests, fuzzing, and benchmarks

The current platform demos use a fixed **320×240 RGB565 logical image**, but the
renderer core itself is not hard-wired to 320×240: `gfx_init()` receives the
target width and height from its caller. Pico and Raylib present RGB565 directly.
DOS runs the same RGB565 shared code, then quantizes each completed region to a
fixed RGB332 VGA palette while presenting to 320×240 Mode X.

The renderer owns no framebuffer allocation. Each frontend supplies either a
small tile or a full-frame working buffer, plus presentation callbacks. That is
what allows the same drawing and game code to run on segmented 16-bit DOS,
RP2350, and desktop hosts.

#### Pico2 - 110 FPS
![Pico 2 capture](https://raw.githubusercontent.com/SaxonRah/microrender/main/pico2_screenshot.png)
#### Pico2 - 76 FPS
![Pico 2 capture](https://raw.githubusercontent.com/SaxonRah/microrender/main/microrender/pico2_screenshot.png)

## Architecture
```
                    microrender_gfx
                          │
                    NO CLOCK / FPS
                          │
              unrestricted rendering
                          │
          ┌───────────────┴───────────────┐
          │                               │
         GAME                           STRESS
          │                               │
 deterministic fixed 60 Hz        deterministic 1 tick/frame
          │                               │
 render can be 53 FPS             runs as fast as target can
 or 4300+ FPS                     render
          │                               │
 game speed unchanged             workload rate == render rate
 ```

## Clone and initialize dependencies

Raylib is pinned as a Git submodule under `third_party/raylib`.

```powershell
git clone --recurse-submodules https://github.com/SaxonRah/microrender.git
cd microrender
```

For an existing checkout:

```powershell
git submodule sync --recursive
git submodule update --init --recursive
```

`mr build raylib` also attempts to initialize the pinned submodule automatically
when the checkout has Git metadata and the submodule is missing.

## One build entry point

Run commands from the repository root through `mr.bat`:

```powershell
.\mr.bat help
.\mr.bat build tests
.\mr.bat build dos mode=both tile=16 vsync=0
.\mr.bat build pico game
.\mr.bat build pico stress-lace sprites=1024 sys=300000 spi=75000000 lace=8 hud=2
.\mr.bat build raylib demo=game mode=tiled tile=16 scale=3 fps=0
.\mr.bat build all
```

Both `key=value` and quoted `"key=value"` forms are accepted. Build variants are
cache settings, not source rewrites. See [Building.md](Building.md) and
[SCRIPTS.md](SCRIPTS.md).

[DECISIONS.md](DECISIONS.md) records the optimizations that were tried on real
hardware, with the prediction, the measurement, and the mechanism behind each
outcome. Most of them failed, which is the useful part: the arithmetic was
right and the model of the hardware underneath it was wrong, in four different
places.

## Presentation paths

### Raw reference path

```text
clear complete logical frame
draw everything
send or upload complete frame
begin the next frame
```

The raw path deliberately prevents drawing/presentation overlap:

- Pico uses one 320×240 RGB565 buffer and a synchronous LCD transfer.
- DOS uses an Open Watcom huge-memory 150 KiB RGB565 frame, then converts and
  uploads the complete image to Mode X.
- Raylib uploads one complete RGB565 texture after drawing the full frame.

### Optimized paths

- **Pico game pipeline:** DMA sends frame N while the CPU renders frame N+1.
- **Tiled DOS/Raylib:** completed strips are presented without staging another
  complete logical frame.
- **Lace:** the frame is fully rendered, but alternating row groups are sent on
  successive presentations.
- **Dirty rectangles:** only changed regions are presented; gains depend on how
  static the scene is.
- **Render-only stress:** one proof frame is shown, then rasterization is measured
  without continuous LCD transfer.

The shared game and stress HUDs render current and average FPS. Optional Pico
serial logging is controlled separately from screenshot support.

## Rendering rate versus simulation rate

The renderer has no clock and no FPS policy. A frontend may call it as quickly
as the target can run.

- **Game demo:** deterministic fixed updates at `MR_GAME_TICK_HZ` (60 Hz), with
  unrestricted rendering between updates. Faster hardware draws more frames;
  it does not make the game move faster.
- **Stress benchmark:** one deterministic stress update per rendered benchmark
  frame. This is intentionally frame-coupled so the benchmark remains
  unrestricted and frame N describes the same workload state on every target.

The renderer can also be built by itself, with no game, stress, clock, or
platform frontend:

```text
cmake -S shared -B build-renderer -DMR_BUILD_ENGINE_HELPERS=OFF
cmake --build build-renderer --target microrender_gfx
```

## Shared game demo

The same deterministic game state runs on Pico, DOS, Raylib, and in host tests:

- 1024×1024 tilemap world with a bounded 320×240 camera
- collider-aware player/world limits and solid-tile collision
- walkable blue slowdown terrain
- moving enemies and collectible pickups
- small deterministic RGB565 particles on pickup and enemy collisions
- enemy contact restarts the demo, resets player/camera state, restores every
  pickup, and increments the restart counter
- manual keyboard control or deterministic autoplay

The game test protects camera clamping, actor bounds, tile collision, slowdown
movement, pickup placement/collection, particles, restart behavior, pickup reset,
and the fixed three-pixel player step.

## Universal Pico screenshots

Every current Pico application uses the shared USB screenshot service by default:

- `game` and `game-raw`
- every stress presentation mode
- `lcdtest`
- future Pico frontends that register a draw callback

Screenshot support is controlled by `MR_PICO_SCREENSHOT` and defaults to `ON`.
It does **not** require `serial=ON`; that option only enables verbose FPS/debug
logging.

Install the host dependencies once:

```powershell
py -m pip install pyserial pillow
```

Build and flash any Pico preset, close any serial monitor using the COM port, then
capture:

```powershell
py -u .\microrender\tools_capture_pico_screenshot.py COM5 .\pico2_screenshot.png --timeout 30
```

The host sends `SCREENSHOT` and expects:

```text
MRSHOT1 <width> <height> <RGB565-byte-count>
<raw little-endian RGB565 bytes>
```

The service waits for active LCD DMA, reuses the frontend's existing working
buffer, and streams a newly rendered complete logical frame tile by tile. It does
not reserve a second 150 KiB screenshot framebuffer of its own. (With
`MR_PICO_PRESENT_CORE1=ON` a second frame buffer does exist, for the presenter
rather than for screenshots; the capture service waits for core 1 to finish
before reusing it.) A lace capture is therefore
the clean complete logical frame, not the temporary mixture of row groups visible
on the physical LCD.

## Renderer architecture

```c
void gfx_init(gfx_renderer_t *r, int w, int h,
              gfx_color_t *tile_buffer, int tile_h,
              gfx_flush_fn flush, void *user);
```

Synchronous targets use `gfx_render_tiled()`. A target that can overlap transfer
with rendering installs asynchronous begin/wait callbacks and calls
`gfx_render_tiled_pipelined()` with a second working tile.

| concern | DOS | Pico / Raylib / host |
| --- | --- | --- |
| logical resolution | 320×240 | 320×240 |
| shared renderer colour | RGB565 | RGB565 |
| physical presentation | Mode X with fixed RGB332 palette | RGB565 |
| pointer model | Open Watcom large/far/huge pointers | flat pointers |
| optimized game storage | 16-row RGB565 tile by default | full frame on Pico; configurable tile on Raylib |
| raw storage | 150 KiB huge RGB565 frame | complete RGB565 frame |

## Platform quick starts

### Raylib desktop

The normal build uses the pinned submodule:

```powershell
.\mr.bat build raylib
.\mr.bat run raylib --demo game --mode tiled --tile 16
.\mr.bat run raylib --demo game --mode raw --autoplay
.\mr.bat run raylib --demo stress --mode lace --sprites 1024 --lace-block 4
```

An installed Raylib package can be found by CMake, and a different source checkout
can be supplied explicitly:

```powershell
.\mr.bat build raylib raylib=C:\src\raylib
```

### Pico 2

```powershell
.\mr.bat build pico game
.\mr.bat build pico game-raw
.\mr.bat build pico stress-visible
.\mr.bat build pico stress-raw
.\mr.bat build pico stress-lace sprites=1024 sys=300000 spi=75000000
.\mr.bat build pico stress-render
.\mr.bat build pico stress-dirtyrect
.\mr.bat build pico all
```

Use the final `vscode` token to configure a selected preset into
`microrender/build`, which is the directory used by the repository's VS Code
flash/debug settings:

```powershell
.\mr.bat build pico stress-lace sprites=1024 lace=8 hud=2 vscode
```

Pico builds are forced through Ninja and the Pico ARM GCC toolchain. Before
configuration, the build driver removes caches with the wrong generator/compiler
or absolute source/build paths from a previous checkout location.

### DOS

```powershell
$env:WATCOM = 'C:\WATCOM'
.\mr.bat build dos mode=both tile=16 vsync=0
.\mr.bat run dos /auto
.\mr.bat run dosraw /auto
.\mr.bat run stress 512 2100
.\mr.bat run stressraw 512 2100
```

DOSBox `cycles=max` measures the host rather than a period machine. Set
`MR_DOSBOX_CYCLES` before publishing DOS performance figures.

## Correctness and benchmark

```powershell
.\mr.bat build tests
.\mr.bat test
.\mr.bat bench 200
```

The RGB565 host suite includes:

- byte-for-byte equivalence of colorkey, linear RLE, and row-indexed RLE across
  clipped, offscreen, screen-edge, and tile-seam positions
- raw opaque alignment tests
- clipping, dirty-region, RLE validation, tile-capacity, and collision tests
- deterministic shared-game behavior tests
- four deterministic fuzz seeds covering every drawing entry point and the
  pipelined path

CI runs Linux and macOS with ASan+UBSan and warnings as errors. Windows is pinned
to VS2022 for mandatory MSVC unit, game, and fuzz coverage. A separate headless
job compiles the real Raylib frontend against `tests/raylib_stub` and executes all
four game presentation modes plus the stress path. The benchmark and deterministic
asset-pack checks run in separate jobs. See [tests/README.md](tests/README.md).

The row-start-index RLE path is the intended transparency-preserving fast path.
Run `mr bench` on the target host rather than treating CI FPS as a hardware
promise; the benchmark is informational and deliberately has no pass/fail FPS
threshold.

## Asset pipeline

One manifest under `shared/assets/project.json` produces the disk pack and the
embedded Pico representation:

```powershell
.\mr.bat build assets
```

Generated output is written under `shared/generated/`. CI builds the pack twice,
requires byte-identical output, and validates the `MRP1` header and entry table.

## Repository layout

```text
shared/
  src/                  renderer, camera/collision, game, stress, string helpers
  rp2350/               ILI9341 driver, Pico demos, shared screenshot service
  assets/               source maps, sprites, animation metadata, and audio
  tools/                pack and embedding tools
  generated/            generated pack and embedded C output
microrender/             Pico SDK project, presets, VS Code config, capture tool
microrender_dos/         Open Watcom and VGA Mode X frontend
microrender_raylib/      desktop RGB565 frontend
third_party/raylib/      pinned Raylib submodule
scripts/                 unified build, run, tool-discovery, and clean drivers
tests/                   unit, game, fuzz, benchmark, and headless Raylib shim
```

## Known constraints

- Standard VGA cannot display RGB565 directly; DOS quantizes during presentation.
- DOS raw mode needs a 150 KiB huge-memory framebuffer; optimized DOS avoids that
  full-frame staging allocation.
- Individual DOS RLE pixel pools must remain below a 64 KiB segment.
- Pico raw game/stress modes require a complete 240-row view and tile.
- `dirty`, `dirtyfixed`, and `lcdtest` retain a full frame; large tiles can exhaust
  RP2350 SRAM, so the source warns when those modes use tiles taller than 32 rows.
- Dirty-rectangle gains are scene-dependent.
- Only one process can own the Pico USB CDC port while taking a screenshot.

## License

MIT. See [LICENSE](LICENSE).
