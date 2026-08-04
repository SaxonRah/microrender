# Building MicroRender

Run commands from the repository root through `mr.bat`.

```bat
mr help
```

All shipping frontends use a 320×240 RGB565 logical render target. Build-time
settings are passed as `key=value` arguments; runtime settings use normal
frontend arguments.

---

## Host tests and benchmark

Requires CMake and a C99 compiler.

```bat
mr build tests
mr test
mr bench 200
```

The normal suite is RGB565 and contains unit, shared-game, and deterministic
fuzz tests. The optional `mr test index8` remains useful as a legacy core-format
compatibility check, but no shipping frontend uses it.

---

## Raylib desktop frontend

Raylib gives Windows, Linux, and macOS a real-time host frontend for the same
320×240 RGB565 game and stress demos.

Install Raylib so CMake can find it, set `CMAKE_PREFIX_PATH`, or supply a source
checkout:

```bat
mr build raylib raylib=C:\src\raylib
```

Build defaults are configurable:

```bat
mr build raylib demo=game mode=tiled tile=16 scale=3 fps=0 autoplay=OFF
mr build raylib demo=stress mode=lace sprites=1024 lace=4 fps=0
```

Run-time options can override the compiled defaults:

```bat
mr run raylib --demo game --mode raw --autoplay
mr run raylib --demo game --mode tiled --tile 16 --scale 4
mr run raylib --demo stress --mode dirtyrect --sprites 512
mr run raylib --demo stress --mode lace --sprites 1024 --lace-block 4
```

Modes:

| mode | behavior |
| --- | --- |
| `raw` | draw complete frame, upload complete RGB565 texture, loop |
| `tiled` | upload each completed tile directly |
| `lace` | update alternating row groups |
| `dirtyrect` | compare RGB565 frames and upload one bounding changed rectangle |

Every mode renders FPS and average FPS in the shared HUD.

---

## 16-bit DOS

Requires Open Watcom with `WATCOM` pointing to the install root.

```bat
set WATCOM=C:\WATCOM
mr build dos mode=both tile=16 vsync=0
```

Outputs:

| executable | demo | presentation |
| --- | --- | --- |
| `mrender.exe` | shared game | optimized direct tiled Mode X upload |
| `mraw.exe` | shared game | raw full-frame staging and upload |
| `mstress.exe` | stress | optimized direct tiled upload |
| `msraw.exe` | stress | raw full-frame staging and upload |

Run them through DOSBox:

```bat
mr run dos /auto
mr run dosraw /auto
mr run stress 512 2100
mr run stressraw 512 2100
```

The shared renderer remains 320×240 RGB565. Standard VGA is palettized, so the
final Mode X upload converts RGB565 to a fixed RGB332 palette. Raw mode uses an
Open Watcom huge-memory 150 KiB frame; optimized mode needs only the configured
small tile.

Build settings:

```bat
mr build dos mode=raw tile=16 vsync=0
mr build dos mode=tiled tile=8 vsync=1
mr build dos mode=both tile=16 vsync=0
```

`tile` changes the renderer working-strip height. `vsync` changes the default;
`/vsync` and `/novsync` can still override it at runtime.

---

## Pico 2 / RP2350

Requires Pico SDK, ARM GCC, CMake, and Ninja.

Common presets:

```bat
mr build pico game
mr build pico game-raw
mr build pico stress-visible
mr build pico stress-raw
mr build pico stress-lace
mr build pico stress-render
mr build pico stress-dirtyrect
mr build pico all
```

`mr build pico all` configures and builds every Pico preset. Pico command-line
builds always run from `microrender\`, use the `Ninja` generator and the Pico
ARM GCC toolchain, and automatically remove a stale build directory previously
configured with Visual Studio/MSVC. This makes the command safe to run from an
ordinary PowerShell, Developer PowerShell, or VS Code terminal.

Override preset settings on the same command line:

```bat
mr build pico stress-lace sprites=1024 sys=300000 spi=75000000 lace=4 hud=2
mr build pico stress-visible tile=240 sprites=1024 pipeline=ON serial=ON
mr build pico game presentation=pipelined tile=240 sys=300000 spi=75000000
```

Friendly settings:

| key | CMake option |
| --- | --- |
| `tile=N` | `MR_TILE_H` |
| `sprites=N` | `MR_STRESS_SPRITES` |
| `sys=N` | `MR_PICO_SYS_KHZ` |
| `spi=N` | `MR_LCD_SPI_BAUD` |
| `pipeline=ON/OFF` | `MR_PICO_FRAME_PIPELINE` |
| `mode=NAME` | `MR_STRESS_MODE` |
| `presentation=raw/pipelined` | `MR_GAME_PRESENTATION` |
| `hud=N` | `MR_STRESS_HUD_MODE` |
| `lace=N` | `MR_STRESS_LACE_BLOCK_H` |
| `target=N` | `MR_STRESS_TARGET_FPS` |
| `serial=ON/OFF` | `MR_STRESS_PICO_SERIAL` and `MR_PICO_GAME_SERIAL` |
| `diag=ON/OFF` | `MR_STRESS_PICO_DIAG` |

Any cache variable beginning with `MR_` can also be passed through directly:

```bat
mr build pico stress-dirtyrect MR_STRESS_DIRTY_MERGE_GAP=6 MR_STRESS_DIRTY_FULL_THRESHOLD_PCT=80
```

Raw Pico modes intentionally require `tile=240` and a full 240-row view. CMake
rejects shorter raw configurations because they would no longer represent the
requested clear-frame/draw-everything/send-complete-frame loop.

The generated `.uf2` is in the preset's build directory. Copy it to the Pico in
BOOTSEL mode.

See [PICO_PRESENTATION_MODES.md](PICO_PRESENTATION_MODES.md).

---

## Assets

```bat
mr build assets
```

Regenerates the MRP package and embedded C data. CI builds the pack twice and
requires byte-identical output and a valid MRP header.

---

## Everything available locally

```bat
mr build all
```

This always builds assets and host tests, then attempts DOS, Pico, and Raylib
when their toolchains are available.

```bat
mr clean
```

removes build output.
