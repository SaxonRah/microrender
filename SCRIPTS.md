# Unified command-line scripts

`mr.bat` is the public entry point for build, run, test, benchmark, and clean
operations. Variant settings are command-line arguments rather than scripts that
rewrite CMake or C sources.

```text
.\mr.bat build assets
.\mr.bat build tests
.\mr.bat build dos [mode=raw|tiled|both] [tile=N] [vsync=0|1]
.\mr.bat build pico [preset|all] [key=value ...] [vscode]
.\mr.bat build raylib [key=value ...]
.\mr.bat build all

.\mr.bat run dos [args...]
.\mr.bat run dosraw [args...]
.\mr.bat run stress [sprites] [frames] [args...]
.\mr.bat run stressraw [sprites] [frames] [args...]
.\mr.bat run raylib [args...]

.\mr.bat test [index8]
.\mr.bat bench [frames]
.\mr.bat clean
```

## Option parsing

The worker accepts both spellings below:

```powershell
.\mr.bat build pico stress-lace sprites=1024 sys=300000
.\mr.bat build pico stress-lace "sprites=1024" "sys=300000"
```

This matters because `cmd.exe` can expose an unquoted equals expression to a
called batch file as two parameters. `scripts/mr_build.bat` detects whether a
setting occupied one or two parameters and shifts accordingly. Empty and unknown
settings fail immediately instead of becoming empty C preprocessor definitions.

Pico and Raylib additionally pass any `MR_...=value` cache variable through to
CMake. DOS accepts only its documented aliases.

## Files

| file | role |
| --- | --- |
| `mr.bat` | repository-root validation, command dispatch, and help |
| `scripts/mr_build.bat` | assets, tests, DOS, Pico, Raylib, and aggregate builds |
| `scripts/mr_run.bat` | DOSBox, Raylib, CTest, and benchmark launcher |
| `scripts/mr_tools.bat` | CMake, Open Watcom, and DOSBox discovery |
| `scripts/mr_clean.bat` | build-output cleanup |
| `scripts/mr_frmctr_sweep.bat` | builds one Pico image per ILI9341 panel-refresh setting |
| `scripts/mr_test_raylib.bat` | runs the Raylib frontend across the demo/mode/tile matrix for a fixed frame count |
| `scripts/mr_test_dos.bat` | builds the DOS frontend across its option matrix, optionally running each in DOSBox |
| `scripts/mr_preset_flags.py` | reads Pico preset cache variables and binary directories |
| `microrender/pico_env_auto.bat` | Pico SDK/toolchain/Ninja environment discovery |
| `microrender_dos/build_watcom*.bat` | low-level 16-bit compiler/linker commands |
| `microrender/tools_capture_pico_screenshot.py` | USB RGB565 screenshot receiver and PNG writer |

## Build examples

```powershell
.\mr.bat build dos mode=both tile=16 vsync=0
.\mr.bat build pico game-raw
.\mr.bat build pico stress-lace sprites=1024 sys=300000 spi=75000000 lace=8
.\mr.bat build pico stress-lace sprites=1024 lace=8 vscode
.\mr.bat build pico all
.\mr.bat build raylib
.\mr.bat build raylib raylib=C:\src\raylib demo=game mode=tiled tile=16 scale=3
```

The Raylib path is an override. With no override, the build uses the pinned
`third_party/raylib` submodule and initializes it automatically when possible.

## Run examples

```powershell
.\mr.bat run dos /auto
.\mr.bat run dosraw /auto
.\mr.bat run stress 1024 2100
.\mr.bat run stressraw 512 2100
.\mr.bat run raylib --demo game --mode raw --autoplay
.\mr.bat run raylib --demo stress --mode dirtyrect --sprites 512
```

The run driver forwards Raylib arguments unchanged. For DOS stress, the first two
positional values become `/sprites` and `/frames`; remaining values are forwarded
to the DOS executable.

## Pico preset directories

Each Pico preset has its own `binaryDir`, so different presets never share a
build directory. Different *flags* passed to the same preset do share one:
`build pico stress-lace MR_FOO=ON` and `build pico stress-lace` both land in
`build-stress-lace`.

CMake cache variables persist, so the second command does not clear `MR_FOO` --
it simply never mentions it. Left alone, that produces a binary that does not
match the command that built it.

`mr_build.bat` therefore stamps the flags into `.mr_build_flags` inside the
build directory and wipes it when they change, printing what differed:

```text
[pico] build flags changed since this directory was configured.
       was: stress-lace  -DMR_STRESS_LACE_BLOCK_H=4
       now: stress-lace  -DMR_STRESS_LACE_BLOCK_H=8
```

The wipe retries for a few seconds. Windows can hold a handle on freshly
written build output after the writing process exits -- `.ninja_log` is the
usual casualty, and antivirus scanning a just-finished build makes it common
rather than rare. If it still fails, close any editor or terminal sitting in
that directory.

The directory is also wiped when the generator, toolchain, source path, or
build path no longer match the cache.

## Frontend smoke tests

The host suites (`mr.bat test`) check rendering output but never construct a
frontend, so they cannot catch a demo/mode/tile combination that fails to
start. Two scripts cover that gap.

```powershell
.\scripts\mr_test_raylib.bat        rem 120 frames per combination
.\scripts\mr_test_raylib.bat 600    rem longer runs
```

Fourteen combinations across both demos, every presentation mode, several tile
heights, and the sprite-count edges. `--frames N` closes each window on its own
so the matrix runs unattended, and each combination is reported pass or fail
with a summary at the end.

```powershell
.\scripts\mr_test_dos.bat           rem build only
.\scripts\mr_test_dos.bat run       rem also launch each in DOSBox
```

Seven builds across `raw`/`tiled`/`both`, tile heights, and vsync. Build-only by
default because running needs DOSBox and is not unattended.

DOS is the target most worth exercising. It is the only build with 16-bit `int`
and far pointers, so shared code that compiles and passes tests everywhere else
can still fail there -- pointer arithmetic assuming a flat address space, or an
`int` that overflows at 32,767. CI does not cover it, because Open Watcom is
not installed on the runners.

## Raylib submodule behavior

When no `raylib=PATH` override is supplied, the build checks
`third_party/raylib/CMakeLists.txt`. If missing, it runs a shallow recursive
submodule update first, retries a full pinned checkout if needed, and fails with
manual recovery commands if initialization still fails.

## Aggregate build behavior

`.\mr.bat build all` continues through independent platform failures and reports
a combined failure at the end. Assets are mandatory. DOS is skipped when
`WATCOM` is unset. Missing or broken Pico/Raylib prerequisites make the aggregate
command fail rather than printing a false success.

## DOSBox cycles

`cycles=max` measures the host rather than a historical PC. Pin
`MR_DOSBOX_CYCLES` for reproducible comparisons:

```powershell
$env:MR_DOSBOX_CYCLES = 'fixed 12000'
.\mr.bat run stress 512 2100
```

Approximate examples used by the script comments:

| intended class | value |
| --- | --- |
| 386DX/33 | `fixed 3000` |
| 486DX2/66 | `fixed 12000` |
| Pentium 100 | `fixed 30000` |
| host-unbounded | `max` |

## Important environment overrides

| variable | purpose |
| --- | --- |
| `WATCOM` | Open Watcom installation root |
| `DOSBOX_EXE` | full path to DOSBox-X or DOSBox executable |
| `MR_DOSBOX_CYCLES` | DOSBox CPU cycle setting |
| `PICO_SDK_PATH` | Pico SDK root, normally filled by `pico_env_auto.bat` |
| `PICO_TOOLCHAIN_PATH` | Pico ARM GCC toolchain root |
| `PICO_BOARD` | board name; defaults to `pimoroni_pico_plus2_rp2350` |
