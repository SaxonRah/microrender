# Script consolidation

The original repository carried 37 individual batch/PowerShell scripts. Most differed only in one or two hardcoded arguments: six `run_stress_*` variants
that changed sprite count and a render-only flag, three `set_pico_*` wrappers
around a source-rewriting Python patcher, two `copy_to_*` aliases for the same
copy. I've consolidated all scripts into a single script chain with arguments. 

Variants are now arguments. Five scripts remain.

| script | role |
| --- | --- |
| `mr.bat` | single entry point; dispatches everything below |
| `scripts/mr_tools.bat` | locates Open Watcom, DOSBox and CMake, once |
| `scripts/mr_build.bat` | assets, DOS, Pico, host tests |
| `scripts/mr_run.bat` | DOSBox launcher, ctest, benchmark |
| `scripts/mr_clean.bat` | removes build output |

```
mr build assets | dos | pico [preset] | tests | all
mr run dos [args...]                 game demo in DOSBox
mr run stress [sprites] [frames]     stress test in DOSBox
mr test [index8]                     host suite under ASan/UBSan
mr bench [frames]                    host benchmark
mr clean
```

Pico variants are CMake presets rather than scripts: `game`, `stress-visible`,
`stress-lace`, `stress-render`, `stress-dirtyrect`.

## What the old scripts map to

| Example Script Commands |
| --- |
| `mr build all` |
| `mr build assets` |
| `mr build dos` |
| `mr build pico` |
| `mr build dos && mr run dos` |
| `mr build dos && mr run stress 512` |
| `mr run stress 1024` |
| `mr run stress 1024 2100 /noflush` |
| `mr run dos` |
| `mr run dos /auto` |
| `mr run stress <n> <frames>` |
| `mr build dos` |
| `mr build pico stress-visible` |
| `mr build pico stress-lace` |
| CMake presets |

`build_watcom.bat` and `build_watcom_stress.bat` are kept as the actual Open
Watcom invocations and are called by `mr build dos`. `start_watcom_here.bat`
is kept for interactive use. `microrender/pico_env_auto.bat` is kept because
the Pico VS Code extension expects it.

## Benchmark cycles

Both old DOSBox runners hardcoded `cycles=max`, which measures the host CPU
rather than a period machine. `mr run` reads `MR_DOSBOX_CYCLES` instead:

| target | value |
| --- | --- |
| 386DX/33 | `fixed 3000` |
| 486DX2/66 | `fixed 12000` |
| Pentium 100 | `fixed 30000` |
| unbounded (default) | `max` |

```bat
set MR_DOSBOX_CYCLES=fixed 12000
mr run stress 512 2100
```

The launcher also now sets `machine=vgaonly` for both binaries (only one of the
old runners did), and runs DOSBox in the foreground so the exit code reaches
capture scripts. The old `run_dosbox_mrender.bat` used `start ""` and therefore
always reported success.
