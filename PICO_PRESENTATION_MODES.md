# Pico 2 presentation modes

The stress test can present a frame several different ways. Rendering cost is
identical across all of them; what changes is how much pixel data goes out over
SPI and how it is spread across frames. Select one with `MR_STRESS_MODE`.

Measurements below are the 1024-sprite stress scene on the tested ILI9341 panel
at 300 MHz system clock and 75 MHz SPI.

| mode | what it does | FPS | artifacts |
| --- | --- | ---: | --- |
| `visible` | uploads every RGB565 pixel every frame | ~55.8 | none |
| `lace` | full 320x240 render, alternating row groups presented per frame | ~76–77 | temporal row tearing on fast motion |
| `dirtyrect` | coalesced dirty rectangles only | scene-dependent | none |
| `everyN` | full upload every Nth frame | ~N x | visible stutter |
| `render` | rasterize only, one proof frame then no further flushes | renderer ceiling | nothing displayed |

`render` is the one to use when measuring the rasterizer rather than the panel:
it removes SPI bandwidth from the loop entirely.

## Building a mode

```bat
mr build pico stress-visible
mr build pico stress-lace
mr build pico stress-render
mr build pico stress-dirtyrect
```

Or directly, when you want to vary something a preset does not cover:

```bat
cmake -S microrender -B microrender/build ^
      -DMR_APP=STRESS -DMR_STRESS_MODE=lace -DMR_STRESS_SPRITES=1024 ^
      -DMR_PICO_SYS_KHZ=300000 -DMR_LCD_SPI_BAUD=75000000 ^
      -DMR_VIEW_H=240 -DMR_STRESS_HUD_MODE=2 -DMR_STRESS_LACE_BLOCK_H=4
cmake --build microrender/build
```

`cmake -LH -S microrender -B microrender/build` lists every option with its
documentation.

## Mode names

Each name maps to a numeric flush mode plus an orthogonal fixed-camera flag:

| `MR_STRESS_MODE` | flush mode | fixed camera |
| --- | ---: | ---: |
| `visible` | 0 | no |
| `render` | 1 | no |
| `fixedrender` | 1 | yes |
| `everyN` | 2 | no |
| `dirty` | 3 | no |
| `dirtyfixed` | 3 | yes |
| `lcdtest` | 4 | no |
| `dirtyrect` | 5 | no |
| `dirtyrectfixed` | 5 | yes |
| `lace` | 6 | no |
| `lacefixed` | 6 | yes |

The fixed-camera variants hold the camera still so the dirty-rect and caching
paths see a static background, which is the case they are designed for.

`dirty` and `lcdtest` reserve a full-frame buffer, so pair them with
`-DMR_TILE_H=16`. CMake warns at configure time if you do not.
