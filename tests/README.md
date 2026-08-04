# MicroRender host tests

The host suite builds the shared 320×240 RGB565 renderer and game code without
requiring the Pico SDK, Open Watcom, DOSBox, or Raylib.

```sh
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

## Tests

### `mr_test_unit.c`

Behavioral renderer tests. The main equivalence test renders a sprite through
the three transparency-preserving paths—per-pixel colorkey, linear-scan RLE,
and row-indexed RLE—at 132 screen-edge, tile-seam, clipped, partially offscreen,
and fully offscreen positions, then compares the resulting framebuffers byte
for byte.

Raw opaque drawing intentionally has a separate alignment test because it
writes pixels that transparent paths skip.

Other coverage includes clipping, rectangle algebra, dirty-list merging and
fallback, tile-capacity clamping, RLE validation failures, degenerate sprites,
and collision resolution at negative coordinates.

### `mr_test_game.c`

Deterministic shared-game behavior:

- 320×240 screen and 1024×1024 stage dimensions
- camera clamping at every world edge
- collider-aware player stage bounds
- pickup collection and particle creation
- enemy contact restart
- complete pickup reset after restart
- player/camera reset and FPS/debug-state preservation
- prevention of the old double-movement bug

### `mr_test_fuzz.c`

Calls every drawing entry point with coordinates from -4000 to +4000 through
randomized clip windows, sub-region passes, and the pipelined double-buffered
path. The flush callback independently validates every rectangle. CTest
registers four deterministic seeds.

### `mr_test_bench.c`

Optimized host benchmark for blit paths and tile-height sweeps. It is not linked
against sanitizers because sanitizer overhead would invalidate the comparison.

## Options

| option | default | meaning |
| --- | --- | --- |
| `MR_TESTS_SANITIZE` | on in Debug for GCC/Clang | ASan + UBSan; optional ASan on MSVC |
| `MR_TESTS_WERROR` | `ON` | warnings are errors |
| `MR_TESTS_INDEX8` | `OFF` | optional legacy core-format compatibility build; shipping frontends are RGB565 |
| `MR_FUZZ_ITERATIONS` | 300 | iterations per registered fuzz seed |

CI requires RGB565 unit, game, and fuzz success on pinned Linux, macOS, and
Windows/VS2022 jobs. Linux and macOS use ASan+UBSan; Windows uses the ordinary
MSVC runtime because hosted MSVC AddressSanitizer previously hung.

## Raylib frontend CI check

`tests/raylib_stub/` is a small headless API shim used only in CI. Actions
builds the real `microrender_raylib/main.c` frontend against it and executes
raw, tiled, lace, dirty-rectangle, game, and stress paths for a finite number
of frames. Normal desktop builds still require real Raylib.

