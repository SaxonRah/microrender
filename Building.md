# Building MicroRender

Run commands from the repository root. PowerShell requires the explicit relative
path:

```powershell
.\mr.bat help
```

All platform frontends use a fixed 320×240 RGB565 logical render target. Build
settings use `key=value`; runtime frontend settings use their normal arguments.
Both unquoted `key=value` and quoted `"key=value"` forms are accepted by the batch
parser.

## Source checkout and Raylib submodule

Raylib is pinned as `third_party/raylib`.

```powershell
git clone --recurse-submodules https://github.com/SaxonRah/microrender.git
cd microrender
```

For an existing checkout:

```powershell
git submodule sync --recursive
git submodule update --init --recursive
```

The Raylib build driver attempts the same initialization automatically when the
submodule is absent and the source tree is a Git checkout.

## Host tests and benchmark

Requires CMake and a C99 compiler.

```powershell
.\mr.bat build tests
.\mr.bat test
.\mr.bat bench 200
```

`mr test` configures and executes six CTest entries: unit, shared game behavior,
and four deterministic fuzz seeds. The default test build is RGB565. The optional
legacy indexed-colour compatibility build is:

```powershell
.\mr.bat test index8
```

No shipping frontend uses the indexed host configuration.

## Raylib desktop frontend

The normal command uses the pinned submodule:

```powershell
.\mr.bat build raylib
```

Resolution order in `microrender_raylib/CMakeLists.txt` is:

1. explicit `raylib=PATH` / `MR_RAYLIB_PATH`
2. `third_party/raylib`
3. an installed CMake Raylib package
4. an installed `raylib.h` plus Raylib library

Examples:

```powershell
.\mr.bat build raylib demo=game mode=tiled tile=16 scale=3 fps=0 autoplay=OFF
.\mr.bat build raylib demo=stress mode=lace sprites=1024 lace=4 fps=0
.\mr.bat build raylib raylib=C:\src\raylib
```

Runtime settings override compiled defaults:

```powershell
.\mr.bat run raylib --demo game --mode raw --autoplay
.\mr.bat run raylib --demo game --mode tiled --tile 16 --scale 4
.\mr.bat run raylib --demo stress --mode dirtyrect --sprites 512
.\mr.bat run raylib --demo stress --mode lace --sprites 1024 --lace-block 4
```

| mode | behavior |
| --- | --- |
| `raw` | draw complete frame, upload complete RGB565 texture, loop |
| `tiled` | upload each completed tile |
| `lace` | update alternating row groups |
| `dirtyrect` | compare complete frames and upload one bounding changed rectangle |

Additional runtime options are `--fps N`, `--frames N`, and `--help`.

## 16-bit DOS

Requires Open Watcom with `WATCOM` pointing to its install root.

```powershell
$env:WATCOM = 'C:\WATCOM'
.\mr.bat build dos mode=both tile=16 vsync=0
```

The build regenerates assets first. Both game and stress links include the shared
`mr_strbuf.c` implementation used by the FPS HUD.

| executable | demo | presentation |
| --- | --- | --- |
| `mrender.exe` | shared game | optimized direct tiled Mode X upload |
| `mraw.exe` | shared game | raw full-frame staging and upload |
| `mstress.exe` | stress | optimized direct tiled Mode X upload |
| `msraw.exe` | stress | raw full-frame staging and upload |

Run through DOSBox:

```powershell
.\mr.bat run dos /auto
.\mr.bat run dosraw /auto
.\mr.bat run stress 512 2100
.\mr.bat run stressraw 512 2100
```

Build variants:

```powershell
.\mr.bat build dos mode=raw tile=16 vsync=0
.\mr.bat build dos mode=tiled tile=8 vsync=1
.\mr.bat build dos mode=both tile=16 vsync=0
```

`tile` sets the RGB565 working-strip height. `vsync` sets the compiled default;
`/vsync` and `/novsync` can override it at runtime. Standard VGA is palettized,
so the Mode X frontend converts shared RGB565 output to a fixed RGB332 palette.

## Pico 2 / RP2350

Requires the Pico SDK, ARM GCC, CMake, Ninja, and Python. The repository defaults
to Pico SDK 2.2.0, toolchain `14_2_Rel1`, and the
`pimoroni_pico_plus2_rp2350` board definition.

### Presets

```powershell
.\mr.bat build pico game
.\mr.bat build pico game-raw
.\mr.bat build pico stress-visible
.\mr.bat build pico stress-raw
.\mr.bat build pico stress-lace
.\mr.bat build pico stress-render
.\mr.bat build pico stress-dirtyrect
.\mr.bat build pico all
```

`pico all` builds the seven presets above. The driver always configures from
`microrender/`, forces Ninja, and puts the Pico ARM GCC toolchain first on `PATH`.
Before configuring, it validates the cached generator, compiler, source directory,
and cache directory. A moved checkout or a cache made by Visual Studio/MSVC is
deleted and configured again automatically.

### Command-line overrides

```powershell
.\mr.bat build pico stress-lace sprites=1024 sys=300000 spi=75000000 lace=8 hud=2
.\mr.bat build pico stress-visible tile=240 sprites=1024 pipeline=ON serial=ON
.\mr.bat build pico game presentation=pipelined tile=240 sys=300000 spi=75000000
```

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
| `serial=ON/OFF` | stress/game verbose USB logs |
| `diag=ON/OFF` | `MR_STRESS_PICO_DIAG` |

Presentation and panel options have no short alias and are passed by their full
cache variable name:

| variable | default | purpose |
| --- | --- | --- |
| `MR_PICO_PRESENT_CORE1` | `ON` for `stress-lace`, otherwise `OFF` | present lace row groups from core 1 while core 0 renders the next frame; costs a second 150 KiB frame buffer |
| `MR_STRESS_LACE_PHASES` | `2` | `1` presents every row every frame instead of alternating groups |
| `MR_ILI9341_FRMCTR1_RTNA` | `0x1B` | panel refresh rate; lower is faster, and too low washes the display out |
| `MR_ILI9341_FRMCTR1_DIVA` | `0x00` | panel oscillator division ratio |

`MR_ILI9341_FRMCTR1_*` defaults reproduce the ILI9341 reset state and the
register write is compiled out entirely unless changed. See
`PICO_PRESENTATION_MODES.md` for a measured sweep; on the tested panel nothing
above the default helped and the faster settings degraded contrast.

Any cache variable beginning with `MR_` can be passed directly:

```powershell
.\mr.bat build pico stress-dirtyrect MR_STRESS_DIRTY_MERGE_GAP=6 MR_STRESS_DIRTY_FULL_THRESHOLD_PCT=80
.\mr.bat build pico game MR_PICO_SCREENSHOT=OFF
```

Raw game mode requires `MR_TILE_H=240`. Raw stress mode requires both
`MR_TILE_H=MR_VIEW_H` and a complete 240-row view. CMake rejects configurations
that would stop being the intended clear/draw-complete-frame/send baseline.

### VS Code flash/debug build directory

Append `vscode` to configure one selected preset into `microrender/build`:

```powershell
.\mr.bat build pico stress-lace sprites=1024 lace=8 hud=2 vscode
```

The repository's VS Code project then uses:

```text
microrender/build/microrender.elf
microrender/build/microrender.uf2
```

Without `vscode`, each preset uses the `binaryDir` declared in
`microrender/CMakePresets.json`.

### USB screenshot capture

`MR_PICO_SCREENSHOT` defaults to `ON` for every Pico app and automatically enables
USB stdio. `serial=ON` is not required; it only enables FPS/debug text.

```powershell
py -m pip install pyserial pillow
py -u .\microrender\tools_capture_pico_screenshot.py COM5 .\pico2_screenshot.png --timeout 30
```

Close VS Code's serial monitor or any other program holding the port. The capture
script retries `SCREENSHOT` while USB enumerates, waits for `MRSHOT1`, receives the
exact RGB565 byte count, converts to RGB888, and writes PNG. It accepts `--baud`
and `--timeout`; USB CDC ignores the nominal wire baud value.

The firmware supports `SCREENSHOT`, `SHOT`, `PING`, and `HELP`. Captures are newly
rendered complete logical frames and reuse the existing application buffer.

See [PICO_PRESENTATION_MODES.md](PICO_PRESENTATION_MODES.md).

## Assets

```powershell
.\mr.bat build assets
```

This regenerates `shared/generated/GAME.MRP`, its generated header, and embedded C
bytes from `shared/assets/project.json`. CI builds the pack twice and requires
byte-identical output plus a valid `MRP1` header.

## Build everything available locally

```powershell
.\mr.bat build all
```

Behavior is intentionally explicit:

- assets are mandatory
- host tests are attempted and failures are remembered
- DOS is built only when `WATCOM` is set
- Pico and Raylib are attempted when CMake is available
- Raylib initializes its submodule when needed
- all remaining targets continue after a failure
- the final process exits nonzero if any attempted target failed

A missing Pico SDK is therefore a failure, not a silent skip. A missing `WATCOM`
environment variable is the one normal platform skip.

## Clean

```powershell
.\mr.bat clean
```

This removes generated build output; it does not remove source assets or the
Raylib submodule checkout.
