# Pico 2 presentation modes

Pico game and stress builds use a fixed 320×240 RGB565 logical framebuffer and
render FPS plus average FPS in the HUD.

## Game modes

| `MR_GAME_PRESENTATION` | behavior |
| --- | --- |
| `raw` | clear/draw the complete 320×240 frame, synchronously send it, loop |
| `pipelined` | use DMA so frame N is sent while frame N+1 is rendered |

```bat
mr build pico game
mr build pico game-raw
mr build pico game presentation=pipelined tile=240 sys=300000 spi=75000000
```

Raw game mode requires `MR_TILE_H=240`. It deliberately allocates no second
render buffer, preserving SRAM while remaining a true serialized baseline.

## Stress modes

| mode | what it measures or presents | artifacts |
| --- | --- | --- |
| `raw` | complete render followed by synchronous full-frame upload | none |
| `visible` | optimized full-frame RGB565 DMA presentation | none |
| `lace` | alternating row groups per presentation | faint temporal shimmer on fast motion |
| `dirtyrect` | changed rectangles only | none; gain is scene-dependent |
| `everyN` | full upload every Nth rendered frame | visible stutter |
| `render` | rasterization after one proof frame; no ongoing LCD transfer | display stops changing |
| `dirty` | older full-frame comparison path | none |
| `lcdtest` | LCD-focused diagnostic path | diagnostic only |

Known hardware measurements for the 1,024-sprite scene at 300 MHz system clock
and an actual 75 MHz SPI clock:

| mode | result |
| --- | ---: |
| `visible` | about 55.8 FPS |
| `lace` | about 76–77 FPS |
| `dirtyrect` | scene-dependent and configured separately |

These are not interchangeable configurations. The provided `dirtyrect` preset
uses 512 sprites and 16-row tiles.

## Build examples

```bat
mr build pico stress-raw
mr build pico stress-visible
mr build pico stress-lace sprites=1024 lace=4 sys=300000 spi=75000000
mr build pico stress-render sprites=1024 serial=ON
mr build pico stress-dirtyrect sprites=512 tile=16
mr build pico all
```

`mr build pico all` is the command-line smoke test for the complete preset
matrix. Every preset uses Ninja and the Pico ARM GCC toolchain. Incompatible
Visual Studio/MSVC caches are removed automatically before reconfiguration.

Raw stress mode requires a 240-row tile and full 240-row view. CMake rejects a
partial-height raw configuration.

## Friendly names and numeric modes

| name | numeric flush mode | fixed camera |
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
| `raw` | 7 | no |

Fixed-camera variants isolate presentation behavior from background movement.
Modes that retain a previous full frame should use small tiles to stay within
RP2350 SRAM.
