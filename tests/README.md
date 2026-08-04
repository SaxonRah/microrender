# MicroRender host tests

The host suite compiles the shared renderer, string helpers, and game code without
requiring the Pico SDK, Open Watcom, DOSBox, or a real Raylib installation.

From the repository root on Windows:

```powershell
.\mr.bat build tests
.\mr.bat test
```

Portable CMake commands:

```sh
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

## Registered tests

CTest registers six deterministic entries.

### `mr_test_unit.c`

Behavioral renderer tests. The main equivalence test renders a sprite through the
three transparency-preserving paths—per-pixel colorkey, linear-scan RLE, and
row-indexed RLE—at 132 screen-edge, tile-seam, clipped, partially offscreen, and
fully offscreen positions, then compares the framebuffers byte for byte.

Raw opaque drawing has a separate alignment test because it writes pixels that
transparent paths skip. Additional coverage includes clipping, rectangle algebra,
dirty-list merging/fallback, tile-capacity clamping, RLE validation failures,
degenerate sprites, and collision resolution at negative coordinates.

### `mr_test_game.c`

Deterministic shared-game behavior, including:

- 320×240 screen and 1024×1024 stage dimensions
- camera clamping at every world edge
- collider-aware player stage bounds
- solid tiles and walkable slowdown terrain
- pickup placement, collection, and particle creation
- enemy-contact restart and pickup restoration
- player/camera reset and FPS/debug-state preservation
- prevention of the previous double-movement bug

### `mr_test_fuzz.c`

Calls every drawing entry point with coordinates from -4000 to +4000 through
randomized clip windows, sub-region passes, and the pipelined double-buffered
path. The flush callback independently validates every rectangle. CTest registers
four reproducible seeds: `0x5EED`, `0xC0FFEE`, `0xBADF00D`, and `0x1337`.

### `mr_test_bench.c`

Optimized host benchmark for blit paths and tile-height sweeps. It is built as a
separate executable, not registered as a correctness test, and is never linked to
sanitizers because sanitizer overhead would invalidate comparisons.

```powershell
.\mr.bat bench 200
```

CI records the complete output but does not gate on machine-specific FPS.

## CMake options

| option | default | meaning |
| --- | --- | --- |
| `MR_TESTS_SANITIZE` | `ON` for non-MSVC Debug; otherwise `OFF` | ASan+UBSan on GCC/Clang; opt-in ASan on MSVC |
| `MR_TESTS_WERROR` | `ON` | warnings in the shared core are errors |
| `MR_TESTS_INDEX8` | `OFF` | legacy indexed-colour compatibility build |
| `MR_FUZZ_ITERATIONS` | `300` | iterations per registered fuzz seed |
| `MR_BENCH_FRAMES` | `200` | default benchmark frames per configuration |

The shared core is compiled under strict warnings including shadowing,
conversions, missing prototypes, cast qualification, and undefined-macro checks.

## CI matrix

The workflow contains separate jobs for:

- Ubuntu 24.04 RGB565 with ASan+UBSan
- macOS 15 RGB565 with ASan+UBSan
- Windows Server 2022 with Visual Studio 17 2022 and MSVC warnings as errors
- headless Raylib frontend execution
- informational host benchmark
- deterministic asset-pack generation/validation

Windows sanitizer execution is deliberately disabled in CI because hosted newer
MSVC sanitizer jobs previously hung. Windows still provides mandatory native MSVC
unit, game, and fuzz coverage; Linux/macOS provide mandatory sanitizer coverage.

## Raylib frontend CI shim

`tests/raylib_stub/` implements only the Raylib API subset used by
`microrender_raylib/main.c`. CI compiles the real desktop frontend against this
headless shim and executes:

- game: `raw`, `tiled`, `lace`, and `dirtyrect`
- stress: `tiled`

Each invocation exits after a finite `--frames` count. The shim validates frontend
compilation and control flow without a window server or GPU.

Normal desktop builds do not use the shim. They prefer the pinned
`third_party/raylib` submodule, while an installed package or explicit
`raylib=PATH` remains available as an override.
