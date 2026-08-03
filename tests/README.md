# MicroRender host tests

Builds the shared renderer core for the host so it can be unit tested, fuzzed
and benchmarked without a Pico SDK, Open Watcom or DOSBox.

```sh
cmake -S tests -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

Or with presets: `cmake --preset debug && ctest --preset debug`.

## What is here

**`mr_test_unit.c`** — behavioural assertions, not smoke tests.

The centrepiece is `test_blit_path_equivalence`. The three
transparency-preserving blit paths (colorkey, linear-scan RLE, RLE with a
row-start index) are three implementations of one specification, so they must
produce identical output. The test renders the same sprite through all three at
132 positions — every screen edge, both
sides of every 16-row tile seam, partially and fully offscreen — and compares
framebuffers byte for byte. A disagreement localises the bug to whichever path
is the odd one out, which is what makes it safe to keep optimising the RLE
path. The raw opaque path is excluded on purpose: it writes every pixel,
including the ones the transparent paths skip, so it implements a different
specification and has its own alignment test.

The rest covers blit pixel alignment, colorkey transparency, `fill_rect` and
clip-window boundaries (inclusive lower, exclusive upper), rect algebra
including the touch-versus-overlap distinction that dirty-rect merging depends
on, dirty list bounding and full-redraw fallback, the tile capacity clamp,
every rejection case in `gfx_sprite_rle_validate()`, degenerate sprites (zero
and negative dimensions, null pixels), and collision resolution at negative
world coordinates.

**`mr_test_fuzz.c`** — every drawing entry point called with coordinates from
-4000 to +4000, through randomised clip windows, sub-region (dirty-rect)
passes, and the pipelined double-buffered path. The flush callback
independently re-validates every rect the renderer hands it, so a bad tile rect
is caught even where it would not have overflowed. Deterministic: takes
iterations and a seed, and ctest registers four seeds.

```sh
./build/tests/mr_test_fuzz 1000 0xC0FFEE
```

**`mr_test_bench.c`** — compares blit paths and sweeps tile height. Always
built optimised and never linked against sanitizers, since a benchmark under
ASan measures ASan.

**`mr_test_support.h`** — framebuffer flush target, deterministic RNG,
assertion macros. The framebuffer is heap-allocated on purpose: under
AddressSanitizer the redzones turn a stray write from a flush into a hard
failure instead of silent corruption of an adjacent global.

## Options

| option | default | meaning |
| --- | --- | --- |
| `MR_TESTS_SANITIZE` | on in Debug | ASan + UBSan with `-fno-sanitize-recover` |
| `MR_TESTS_WERROR` | `ON` | `-Werror` on the shared core |
| `MR_TESTS_INDEX8` | `OFF` | build the core in the DOS 8-bit palette format |
| `MR_FUZZ_ITERATIONS` | 300 | iterations per ctest fuzz run |

CI runs the matrix of both pixel formats across Linux, macOS and Windows.
