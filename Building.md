# Building MicroRender

Everything runs through `mr.bat` at the repository root. Run it from there, not
from a subdirectory.

```bat
mr help
```

---

## Host tests and benchmark

The only target with no external toolchain requirement: CMake and any C99
compiler.

```bat
mr build tests
mr test
mr test index8      :: same suite with the core in the DOS pixel format
mr bench 200
```

`mr test` builds and runs the unit suite plus four fuzz seeds under
AddressSanitizer and UndefinedBehaviorSanitizer. See `tests/README.md`.

---

## 16-bit DOS

Requires Open Watcom, with `WATCOM` pointing at the install root.

```bat
mr build dos
```

Builds both `mrender.exe` (game demo) and `mstress.exe` (stress test), and
stages them in `microrender_dos\dosroot\`.

### Running

```bat
mr run dos                    :: game demo, keyboard control
mr run dos /auto              :: shared autodemo input, no keyboard
mr run dos /frames 2000       :: exit after N frames, for capture scripts

mr run stress                 :: 512 sprites, 2100 frames
mr run stress 1024            :: 1024 sprites
mr run stress 1024 2100 /notri :: extra flags pass through to mstress.exe
```

`mrender.exe` accepts `/auto`, `/frames N`, `/wait` and `/?`. Anything else now
produces a warning rather than being silently ignored.

### Benchmark cycles

DOSBox defaults to `cycles=max`, which measures your host CPU rather than a
period machine. Pin it before quoting any framerate:

```bat
set MR_DOSBOX_CYCLES=fixed 12000
mr run stress 512 2100
```

| target | value |
| --- | --- |
| 386DX/33 | `fixed 3000` |
| 486DX2/66 | `fixed 12000` |
| Pentium 100 | `fixed 30000` |
| unbounded | `max` |

`start_watcom_here.bat` in `microrender_dos\` opens an Open Watcom shell if you
want to drive the compiler directly.

---

## Pico 2 / RP2350

Requires the Pico SDK 2.2.0, ARM GCC, Ninja and CMake — all installed by the
official Raspberry Pi Pico VS Code extension.

```bat
mr build pico                     :: game demo (default preset)
mr build pico stress-visible
mr build pico stress-lace
mr build pico stress-render
mr build pico stress-dirtyrect
```

Copy the resulting `.uf2` to the Pico in BOOTSEL mode.

### Flashing through the debug probe from VS Code

The Pico extension's tasks and the Cortex-Debug launch config are hardcoded to
`microrender/build`, while each preset uses its own directory. Add `vscode` to
configure a preset into `microrender/build` so the extension picks it up:

```bat
mr build pico stress-lace vscode
```

Then open the `microrender` folder in VS Code and either:

- **F5** — flashes through the probe and drops into the debugger, or
- **Ctrl+Shift+P** to **Tasks: Run Task** to **Flash** — flashes and runs
  without attaching a debugger, or
- **Tasks: Run Task** to **Run Project** — loads over USB with `picotool`
  instead of the probe, no wiring needed.

Without the `vscode` argument the extension would flash whatever was previously
in `microrender/build`, which is usually not the variant you just built.

Probe wiring for the Debug Probe or a second Pico running `debugprobe`:

| probe | target |
| --- | --- |
| SWCLK | SWCLK |
| SWDIO | SWDIO |
| GND | GND |

If OpenOCD cannot halt the target — common after flashing firmware that
overclocks — run **Tasks: Run Task** to **Rescue Reset**, then flash again.

Build variants are CMake options, not scripts. To vary something a preset does
not cover:

```bat
cmake -S microrender -B microrender/build ^
      -DMR_APP=STRESS -DMR_STRESS_MODE=lace -DMR_STRESS_SPRITES=1024
cmake --build microrender/build
```

`cmake -LH -S microrender -B microrender/build` lists every option with its
documentation. See `PICO_PRESENTATION_MODES.md` for what each mode does and
what it costs.

The stress build shows FPS, average FPS, and visible/bucket/draw/collision
counts on an on-screen HUD; add `-DMR_STRESS_PICO_SERIAL=ON` (or use the
`stress-render` preset) to get the same over USB serial.

---

## Assets

```bat
mr build assets
```

Regenerates `shared\generated\GAME.MRP` and the embedded C assets from
`shared\assets\project.json`. The Pico build runs this automatically as a CMake
custom command; the DOS build runs it via `mr build dos`.

---

## Everything

```bat
mr build all
```

Builds assets and host tests, then DOS and Pico if their toolchains are
present. Missing toolchains are reported and skipped rather than failing the
run.

```bat
mr clean
```

Removes all build output. Committed generated assets are left alone;
regenerate those with `mr build assets`.
