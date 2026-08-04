# Unified command-line scripts

`mr.bat` is the single public entry point. Build variants are arguments instead
of separate source-patching scripts. Pico and Raylib builds also accept any
`MR_...=value` CMake cache variable for advanced settings not covered by a
friendly alias.

```text
mr build assets
mr build tests
mr build dos [mode=raw|tiled|both] [tile=N] [vsync=0|1]
mr build pico [preset|all] [key=value ...]
mr build raylib [key=value ...]
mr build all

mr run dos [args...]
mr run dosraw [args...]
mr run stress [sprites] [frames] [args...]
mr run stressraw [sprites] [frames] [args...]
mr run raylib [args...]

mr test
mr bench [frames]
mr clean
```

## Files

| file | role |
| --- | --- |
| `mr.bat` | command dispatcher and help |
| `scripts/mr_build.bat` | assets, tests, DOS, Pico, and Raylib configuration/build |
| `scripts/mr_run.bat` | DOSBox, Raylib, ctest, and benchmark launcher |
| `scripts/mr_tools.bat` | tool discovery |
| `scripts/mr_clean.bat` | build-output cleanup |
| `microrender_dos/build_watcom*.bat` | low-level Open Watcom compiler/linker invocations |

## Examples

```bat
mr build dos mode=both tile=16 vsync=0
mr build pico game-raw
mr build pico stress-lace sprites=1024 sys=300000 spi=75000000 lace=4
mr build pico all
mr build raylib raylib=C:\src\raylib demo=game mode=tiled tile=16 scale=3

mr run dos /auto
mr run dosraw /auto
mr run stress 1024 2100
mr run stressraw 512 2100
mr run raylib --demo game --mode raw --autoplay
mr run raylib --demo stress --mode dirtyrect --sprites 512
```

## DOSBox cycles

`cycles=max` measures the host rather than a historical PC. Pin
`MR_DOSBOX_CYCLES` for reproducible comparisons:

```bat
set MR_DOSBOX_CYCLES=fixed 12000
mr run stress 512 2100
```

Approximate examples:

| intended class | value |
| --- | --- |
| 386DX/33 | `fixed 3000` |
| 486DX2/66 | `fixed 12000` |
| Pentium 100 | `fixed 30000` |
| host-unbounded | `max` |
