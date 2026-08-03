# Script consolidation

The repository carried 37 batch/PowerShell scripts. Most differed from a
neighbour only in one or two hardcoded arguments: six `run_stress_*` variants
that changed sprite count and a render-only flag, three `set_pico_*` wrappers
around a source-rewriting Python patcher, two `copy_to_*` aliases for the same
copy.

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

| old | new |
| --- | --- |
| `build_all_targets.bat` | `mr build all` |
| `build_assets_all.bat` | `mr build assets` |
| `build_dos_shared.bat`, `build_and_copy.bat`, `build_dos16.bat` | `mr build dos` |
| `build_pico2_shared.bat`, `microrender/build_pico*.bat` | `mr build pico` |
| `build_copy_run.bat` | `mr build dos && mr run dos` |
| `build_copy_run_stress_512.bat` | `mr build dos && mr run stress 512` |
| `build_copy_run_stress_1024.bat` | `mr run stress 1024` |
| `build_copy_run_stress_*_renderonly.bat` | `mr run stress 1024 2100 /noflush` |
| `run_dosbox_mrender.bat` | `mr run dos` |
| `run_dosbox_dirty.bat` | `mr run dos /auto` |
| `run_stress_*_dosbox.bat` (4 files) | `mr run stress <n> <frames>` |
| `copy_to_dosroot.bat`, `copy_to_dosfiles.bat` | folded into `mr build dos` |
| `pico_clean_pixels.bat` | `mr build pico stress-visible` |
| `pico_fast_fps.bat` | `mr build pico stress-lace` |
| `shared/tools/set_pico_*.bat` | CMake presets |

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
