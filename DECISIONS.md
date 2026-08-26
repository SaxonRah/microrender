# Decisions

A log of changes that were proposed, predicted, measured, and either kept or
thrown away. Written because the reasoning behind a rejected change is usually
more useful than the change that worked, and because most of the arithmetic
below was sound while most of the conclusions were wrong.

Hardware throughout: Pimoroni Pico Plus 2 (RP2350), generic ILI9341 over SPI at
75 MHz, system clock 300 MHz, 1,024 sprites, `stress-lace`.

---

## 1. Overlap transfer with rendering — KEPT, 76.5 → 110.8 FPS

**Observation.** `lace` presents alternating row groups with a blocking flush.
Rasterization and transfer therefore run strictly one after the other.

**Prediction.** Frame time should be roughly the longer of the two rather than
their sum. If rasterization is around 5 ms and transfer around 9 ms, that is
about 110 FPS.

**Why it could not simply be handed to DMA.** Each row group needs its own
CASET/RASET/RAMWR window written in 8-bit mode, and DMA cannot wait for the
shifter to drain before toggling D/C. Something has to sit and wait. Core 1 can
be that something: it owns the panel for a whole frame while core 0 renders the
next one into a second buffer.

**Measured.** 110.0 FPS. `frameUs=9072 cpuUs=5181 flushUs=3892`, 75 KiB/frame.

**Verdict: kept**, behind `MR_PICO_PRESENT_CORE1`, on by default for
`stress-lace`. Costs a second 150 KiB frame buffer — 300 KiB of 520 KiB.

**Caveat that matters more than the number.** The panel scans its own memory
near 70 Hz, so it cannot display 110 distinct images. Judged by eye against the
77 FPS build the difference is small: no change in the background, possibly
slightly less jitter. This is a real throughput result and a marginal visual
one.

---

## 2. Faster SPI clock — REJECTED, does not run

**Observation.** 75 MHz is not a panel limit. The RP2350 baud generator divides
`clk_peri` by `prescale * postdiv`, and from 300 MHz the reachable rates step
150 / 75 / 50 / 37.5. Anything requested between 75 and 150 lands back on 75.
75 MHz is where the divider falls, nothing more.

**Prediction.** `MR_PICO_PERI_PLL_KHZ` already existed to work around exactly
this: run the PLL at 340 MHz, attach `clk_peri` to it, divide `clk_sys` back to
300. That gives an exact 340/4 = 85 MHz SPI clock and about 14% more bandwidth.

**Measured.** White screen, no serial output at all.

**Mechanism.** `stress_configure_split_pll_clock()` passed the *PLL* rate to
`set_sys_clock_khz()`. Asking for a 340 MHz peripheral PLL ran the whole chip —
CPU and XIP flash reads — at 340 MHz before dividing back down, while executing
the dividing code out of that flash.

**Then it was fixed, and still failed.** Rewritten to resolve the PLL settings
without applying them, park `clk_sys` and `clk_peri` on `clk_ref`, retune
`pll_sys`, and bring `clk_sys` back at 300 MHz — never exceeding a rate the
board already runs happily at. Still white, still no serial.

**Verdict: rejected.** Presets removed. The path is compiled out by default
(`MR_PICO_PERI_PLL_KHZ=0`) and is not worth chasing: the panel will not display
much past 86 Hz, and lace already reaches 110 FPS at 75 MHz.

---

## 3. 12-bit colour — REJECTED, 25% fewer bytes bought 1.6% more FPS

**Observation.** The ILI9341 accepts 12 bpp (`COLMOD` DBI=3). Three bytes per
two pixels instead of four: a 320x240 frame drops from 153,600 to 115,200
bytes.

**Prediction.** 25% less to send every frame. At 75 MHz the full-frame ceiling
moves from 61 to 81 FPS — better than lace's 76 *and* updating every pixel every
frame rather than half of them. This looked like the best idea in the list.

**Measured.**

| configuration | KiB/frame | frameUs | FPS | wire rate |
| --- | ---: | ---: | ---: | ---: |
| 16bpp, block_h=4 | 75.00 | 9072 | 110.0 | 68.1 Mb/s (90.9%) |
| 16bpp, block_h=8 | 75.00 | 9017 | 110.8 | 68.1 Mb/s (90.9%) |
| 12bpp, half frame | 56.25 | 8871 | 112.6 | 51.9 Mb/s (69.3%) |
| 12bpp, full frame | 112.50 | 17477 | 57.1 | 52.7 Mb/s (70.3%) |

**Mechanism.** A pixel is no longer a whole number of bytes, so the transfer
must use 8-bit DMA frames. The PL022 reaches about 91% of the SPI clock with
16-bit frames and about 70% with 8-bit ones. Framing overhead consumed the
entire byte saving. Both 16bpp rows sit at 68.1 Mb/s and both 12bpp rows at
about 52.

The full-frame case is the clearest statement of it: 57.1 FPS, against the
55.8 FPS plain 16bpp `visible` mode already managed. The predicted 81 FPS does
not exist on an SPI panel.

**Verdict: rejected.** Driver support removed. `gfx_pack_rgb444()` and its tests
are kept, unused, because the measurement invalidated the *transport*, not the
conversion — on a parallel or 16-bit-framed interface the byte count is what
matters and it becomes worth using again.

**Unexplained.** The 12bpp half-frame case rendered scrambled while the
full-frame case rendered cleanly, with the same packing code. Not investigated,
because the performance data said not to ship the feature either way.

---

## 4. Larger row groups — KEPT, but the reasoning was wrong

**Observation.** At `block_h=4` a frame is 30 row groups, each needing a window
write, an 8/16-bit format switch, and a DMA drain wait. Theoretical wire time
for 75 KiB at 75 MHz is 8.19 ms; measured transfer was 9.07 ms. The 880 µs gap
was attributed to per-block protocol, implying about 29 µs per block.

**Prediction.** Doubling to `block_h=8` halves the number of groups and should
recover roughly 440 µs — about 116 FPS.

**Measured.** 110.8 FPS. A saving of 55 µs, not 440.

**Mechanism.** A block costs about 3.7 µs, not 29. The 880 µs gap is PL022
framing overhead spread across the whole transfer, not window setup. At 90.9%
of the SPI clock there is very little left to reclaim by restructuring the
transfer — which also explains why item 3 failed, and is the same effect seen
from the other direction.

**Verdict: kept** as the default, since it is free and slightly faster. The
analysis that motivated it was wrong by a factor of eight.

---

## 5. Raise the panel refresh rate — REJECTED, no perceptible benefit

**Observation.** The board presents 110 FPS into a panel scanning near 70 Hz.
Roughly 40 of every 110 frames are overwritten in GRAM before being displayed.
`FRMCTR1` sets the panel's scan rate and the driver never wrote it.

**Prediction.** Raising the scan rate recovers those frames. The datasheet
offers up to about 119 Hz.

**Measured**, by eye against the `0x1B` reference:

| RTNA | nominal | result |
| --- | --- | --- |
| `0x1B` | 70 Hz | reset default, clean |
| `0x19` | 76 Hz | no visible difference |
| `0x18` | 79 Hz | no visible difference |
| `0x16` | 86 Hz | no visible difference |
| `0x13` | 100 Hz | noticeably brighter, washed out |
| `0x10` | 119 Hz | washed to near-white, image faint behind it |

**Mechanism.** `RTNA` sets clocks per line. Fewer clocks means less time to
charge each row, so past some point the liquid crystal never fully switches.
The washout is not a bug, it is the physical limit of the panel, and it arrives
well below the datasheet maximum.

**Verdict: rejected.** Default stays at `0x1B`. Everything up to 86 Hz was
indistinguishable and everything past it was worse, so the only settings that
change anything make it worse. The option and `scripts/mr_frmctr_sweep.bat`
remain, since the usable limit is per module and per wiring.

**This closes the question.** 110 FPS is where this hardware ends — not because
the bus cannot go faster, but because nothing downstream can show the result.

---

## 6. Wider pixel copies — KEPT, but not where the win was

**Observation.** The innermost copy loop moved one `gfx_color_t` per iteration:
on Cortex-M33 one `LDRH` and one `STRH` per pixel, wasting half of every bus
cycle, since two adjacent RGB565 pixels are exactly one 32-bit word.

**Measured on the host first, and it got slower.** Raw opaque dropped from 4854
to 3466 FPS.

**Mechanism.** MSVC and GCC auto-vectorize the naive 16-bit loop into SSE,
which the hand-written 32-bit loop blocks. No target this project ships to has
SIMD. The host benchmark was measuring an instruction set that does not exist
on the hardware in question.

**Measured on the target.** Cross-compiled for `cortex-m33`, the mix in
`gfx_blit_sprite_rle_unchecked` changed: `strh` 34 → 4, `ldrh` 40 → 10, word
`str` 15 → 39. GCC had already merged the *fill* loop by itself, because
unaligned `STR` is legal on M-profile; it could not merge the *copy* loop
because it cannot prove `src` and `dst` do not overlap.

**Host A/B of the whole change**, two runs each, showed a clear trend by tile
height: no gain at 4–24 rows, +5.5% at 120, +8.8% at 240. That pattern points at
the per-row multiply that was hoisted out of both RLE loops, not at the wider
copy — tall tiles take the unclipped path where the hoist applies.

**Verdict: kept.** But it does not raise FPS in `visible` or `lace`, because
both are transfer-bound. `cpuUs` sits near 5.2 ms against 9 ms of transfer. It
is headroom.

---

## 7. Simulation tied to frame rate — FIXED

**Observation.** Both demos advanced one simulation step per rendered frame.

| target | frames/sec | relative game speed |
| --- | ---: | ---: |
| real 386DX/33 | ~15 | 1x |
| Pico 2 | ~60 | 4x |
| DOS under DOSBox | 140 | 9x |
| Raylib, default | thousands | 100x+ |

Raylib is the worst case and it is the default: `MR_RAYLIB_DEFAULT_FPS` is 0 and
`SetTargetFPS` is never called unless `--fps` is passed.

**Why not delta-time scaling.** Results would depend on timing jitter, and the
byte-for-byte reproducibility the fuzz and game tests rely on would be gone.
Those tests are what caught two regressions during this work.

**What was done instead.** Fixed timestep with an accumulator. The step stays
identical and deterministic; wall-clock time only decides how many steps run.
`tick()` is untouched, so tests call it directly and determinism is preserved
exactly.

**This also removed a measurement trap.** Before the fix, sprites moved about
45% faster at 110 FPS than at 77, so comparing presentation modes side by side
the faster one *looked* smoother largely because the simulation was running
quicker. That is a property of the benchmark, and easy to credit to the
renderer.

**Two bugs it introduced, both found on hardware.**

*Autoplay turned circles.* The scripted input was indexed by frame counter, so
once simulation stopped advancing once per frame it cycled at the redraw rate —
thousands of direction changes per second on an uncapped window. Now indexed by
simulation tick.

*DOS ran faster than every other platform.* The new microsecond timer latched
PIT channel 0, assuming it counts down by one. The BIOS programs channel 0 in
mode 3, square wave, which decrements by **two** per clock: a 65536-to-0 sweep
takes half a tick and repeats twice per tick. The derived timestamp was a
sawtooth that jumped backwards twice per tick. Through unsigned subtraction each
backwards jump became an enormous delta, which the accumulator clamped to
`max_steps` — up to 700 simulation steps per second instead of 60. Channel 0 is
now put into mode 2 with the same divisor, so it sweeps once per tick and the
18.2 Hz BIOS tick is unaffected.

The accumulator now **drops** implausible deltas rather than clamping them.
Clamping a broken clock to `max_steps` is what turned a bad timer read into a
permanent 11x speedup instead of a visible stutter — a failure mode that hides
itself.

---

## Process failures worth recording

**Four debugging cycles were lost to CMake cache contamination.** Build
directories are chosen by preset, so different `MR_*` overrides passed to the
same preset share one. Cache variables are sticky, so a later command that
simply omits a flag does not clear it. Four builds that were meant to differ all
reported identical timings and an identical 112.5 KiB per frame — a full 12bpp
frame — including the control that was supposed to have neither 12bpp nor
single-phase lace. Two of those cycles were spent suspecting renderer changes
that were innocent. `mr_build.bat` now stamps the flags and wipes on change.

**A stub check that could not fail.** New driver code was syntax-checked against
stubs, and the stub file defined the very function under test rather than
compiling the driver's. A call to a helper that did not exist (`lcd_cs_high`,
where the driver's is `lcd_deselect`) was invisible to it. Checking code against
stubs you wrote yourself proves only that your code is self-consistent.

**Reading a comment instead of the code.** The split-PLL function's comment
described the intended split; its first line passed the PLL rate to
`set_sys_clock_khz()`. One line down would have caught it before it reached
hardware.

---

## Summary

One change worked. Four did not. The arithmetic behind every rejected change was
correct — the models of the hardware underneath it were not, in four different
places: divider granularity, transport framing overhead, protocol cost, and the
charge time of a liquid crystal row.

The single most useful habit in all of it was checking `sentKB` against what the
configuration should be sending. It caught the cache contamination, confirmed
the 12bpp result, and would have caught several of these earlier.
