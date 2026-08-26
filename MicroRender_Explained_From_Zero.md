# MicroRender Explained From Zero

## A beginner-friendly tour of the entire project

**Source basis:** the current MicroRender project tree used in this development workstream, anchored at commit `66cd60fcec6bc66d427055af2b9fe0198a308c17` and including the subsequent build, Raylib, RGB565, game-behavior, and universal Pico screenshot work.

This guide assumes the reader has never written a renderer, never programmed a Raspberry Pi Pico, and may not know what words such as *framebuffer*, *sprite*, *DMA*, *RLE*, or *CMake* mean.

It is also written so it can be read aloud as a video script. Lines marked **On screen** are optional visual suggestions.

---

# Opening: why this explanation exists

A few people responded to the earlier MicroRender video with some version of:

> “I barely understood any of that, but it sounds hard.”

That is fair. The project crosses several subjects at once:

- game programming,
- low-level graphics,
- old DOS hardware,
- a modern microcontroller,
- memory management,
- compression,
- hardware communication,
- build systems,
- and automated testing.

If all of those words arrive at the same time, the project sounds like a wall of jargon. So this explanation starts at the absolute beginning.

The simplest possible description is:

> **MicroRender is a tiny program that knows how to draw a 2D game image, and the same drawing code can run on a modern Raspberry Pi Pico 2, a 16-bit DOS computer, and a desktop Raylib window.**

The difficult part is not drawing a rectangle. The difficult part is arranging the code so that wildly different computers can use the same rectangle-drawing, sprite-drawing, collision, camera, game, and asset logic without copying the entire program three times.

---

# Part 1: the five ideas you need before reading any code

## 1. A pixel

A **pixel** is one colored square on a screen.

MicroRender’s normal logical screen is 320 pixels wide and 240 pixels tall. That is:

```text
320 × 240 = 76,800 pixels
```

Each logical pixel is stored as **RGB565**. RGB means red, green, and blue. The numbers 5, 6, and 5 mean:

- 5 bits are used for red,
- 6 bits are used for green,
- 5 bits are used for blue.

Together that is 16 bits, or 2 bytes, per pixel.

A complete 320×240 image therefore needs:

```text
76,800 pixels × 2 bytes = 153,600 bytes
```

That number matters throughout the project.

A Pico 2 can afford a buffer of that size in some modes. A 16-bit DOS program has awkward 64 KiB memory-segment rules, so one ordinary object cannot simply be a 153,600-byte image. MicroRender therefore cannot assume that every platform can comfortably keep a complete screen image in one normal buffer.

## 2. A frame

A **frame** is one complete image shown by the game.

A game repeatedly does this:

1. Read the player’s controls.
2. Update the game world.
3. Draw a new image.
4. Show that image.
5. Repeat.

If this happens 60 times per second, the game is running at about 60 frames per second, or 60 FPS.

## 3. A renderer

A **renderer** is the part of the program that turns instructions such as:

- draw a red rectangle,
- draw this character sprite,
- draw this section of the map,
- draw this line,
- draw this text,

into colored pixels.

MicroRender is a **software renderer**. That means the CPU calculates the pixels itself. It does not hand the scene to a modern 3D graphics card and ask the GPU to do everything.

## 4. A buffer

A **buffer** is simply a reserved area of memory used to hold data temporarily.

You can imagine a buffer as a tray. The program places pixels on the tray, then hands the tray to something that displays them.

A full-screen buffer is called a **framebuffer**. MicroRender can use one where it makes sense, but its core renderer does not require one.

## 5. A platform

A **platform** is the kind of machine and operating environment on which the program runs.

MicroRender currently has three major frontends:

- **Pico 2 / RP2350:** a microcontroller driving an ILI9341 LCD over SPI.
- **16-bit DOS:** an old-style PC program using VGA Mode X and Open Watcom.
- **Raylib desktop:** a convenient modern window used for testing and demonstration.

The machines display pixels in completely different ways. The shared renderer should not care.

---

# Part 2: the one mental model that explains the whole project

## The reusable painting tray

Imagine that the screen is a wall that is 320 squares wide and 240 squares tall.

A simple renderer might prepare the entire wall-sized painting in memory and then display it. That is the full-framebuffer approach.

MicroRender can instead use a much smaller reusable tray:

```text
320 pixels wide × 16 rows tall
```

The process is:

1. Put the tray over rows 0 through 15.
2. Draw only the parts of the scene that touch those rows.
3. Hand those finished rows to the platform.
4. Reuse the same tray for rows 16 through 31.
5. Continue until the screen is complete.

This is **tiled rendering**. In this project, the “tiles” used as work units are usually horizontal strips. Do not confuse those rendering strips with the 16×16 artwork tiles used to construct a game map. They share the word *tile*, but they are different concepts.

The shared renderer does not know whether the finished strip is:

- copied into VGA memory,
- sent to an LCD over SPI,
- uploaded into a Raylib texture,
- discarded during a renderer-only benchmark,
- or checked by a unit test.

It simply calls a function supplied by the platform. That function is called a **flush callback**.

In plain English, the renderer says:

> “I have finished this rectangle of pixels. Platform, you take it from here.”

That separation is the reason the same core works on all three targets.

## The delivery-company analogy

The renderer is a warehouse packing boxes.

- The **tile buffer** is the packing table.
- Drawing functions place items on the table.
- The **flush callback** is the delivery driver.
- DOS delivers the pixels to VGA memory.
- Pico delivers the pixels over SPI to the LCD.
- Raylib delivers the pixels to a desktop texture.

The warehouse does not need to know what vehicle the driver uses.

## Pipelining: two trays instead of one

The Pico can often do something even better.

It can use DMA to send one tile to the LCD while the CPU draws the next tile into a second buffer.

That is like having:

- tray A currently on the delivery truck,
- tray B being packed in the warehouse.

When the truck finishes with tray A, the trays swap roles.

This overlap is called **pipelining**. It reduces the time during which the CPU is waiting for the LCD transfer.

---

# Part 3: what happens during one game frame

Here is the full journey before we look at individual files.

## Step 1: platform input becomes common input

The platform reads its controls:

- DOS reads keyboard scan codes.
- Raylib asks whether desktop keys are held.
- Pico reads whichever buttons or automatic demo controls are configured.

Each frontend translates those controls into the same small `mr_demo_input_t` structure:

```text
up, down, left, right, action, start, pause, debug, quit
```

The shared game never needs to know how a DOS interrupt or a Raylib keyboard function works.

## Step 2: the game updates

`mr_game_demo_tick()` receives that common input.

It then performs the game-side work:

- decide the desired movement direction,
- choose normal speed or blue-terrain slowdown speed,
- move the player,
- stop the player at solid grey blocks,
- move enemies,
- detect enemy contact,
- restart the scene when hit,
- detect collected pickups,
- create pickup or impact particles,
- update messages,
- update animation,
- move the camera,
- update counters and state.

No pixels are drawn during this stage. This is the simulation stage.

## Step 3: the platform asks the renderer for a frame

The frontend calls one of the renderer’s frame functions, such as:

```text
gfx_render_tiled(...)
gfx_render_tiled_pipelined(...)
gfx_render_tiled_region(...)
```

The renderer selects the next horizontal area of the screen and prepares the tile buffer.

## Step 4: the shared game draws the scene

The renderer calls a shared scene callback. The callback eventually runs `mr_game_demo_render()`.

The game draws, in broad order:

1. the map,
2. actors and pickups,
3. particles and trigger/debug shapes,
4. HUD text,
5. title or pause overlays.

Every drawing function knows the current tile’s position and clip region. Anything outside the current tile is skipped.

## Step 5: the tile is flushed

When the strip is complete, the renderer calls the platform’s flush function.

- Pico starts or performs an SPI transfer.
- DOS converts logical RGB565 pixels to its physical VGA palette representation and writes the correct VGA planes.
- Raylib updates the matching part of a texture.

## Step 6: repeat for every strip

The renderer repeats until the entire requested frame or dirty region has been processed.

## Step 7: repeat for the next frame

The main loop goes back to input and simulation.

That loop is the heartbeat of the program.

---

# Part 4: repository map in plain English

```text
microrender/
│
├── shared/                  Code and data used by every platform
│   ├── src/                 Renderer, engine, game, stress test, pack reader
│   ├── rp2350/              Pico-specific LCD, demo, stress and screenshot code
│   ├── assets/              Source art, maps, audio and asset manifest
│   ├── tools/               Programs that convert assets into runtime formats
│   └── generated/           Generated pack and embedded C data
│
├── microrender/             Pico project, CMake settings and VS Code support
├── microrender_dos/         DOS frontend and Open Watcom build scripts
├── microrender_raylib/      Desktop Raylib frontend
├── tests/                   Unit, game, fuzz, benchmark and headless frontend tests
├── scripts/                 Shared build/run/clean command implementation
├── third_party/raylib/      Pinned Raylib dependency when submodule is initialized
├── .github/workflows/       Automated CI build and test instructions
├── mr.bat                   Main human-facing command
└── README and other docs    Project instructions and design notes
```

The key design rule is:

> **If a feature is part of drawing or game behavior, it belongs in `shared`. If it is only about one machine’s screen, keyboard, window, or toolchain, it belongs in that platform’s frontend.**

---

# Part 5: the shared renderer, file by file

## `shared/src/gfx_config.h`

This file is the portability switchboard.

It defines small compiler-dependent helpers such as:

- how to request inline functions,
- how to describe special pointer types if a compiler needs them,
- whether C99 `restrict` is available,
- whether the platform has a 16-bit `int`,
- the default rendering tile height,
- the maximum number of dirty rectangles.

### Why this exists

A modern desktop compiler, ARM GCC, and 16-bit Open Watcom do not have identical language and memory behavior.

Instead of scattering compiler checks through every drawing function, the differences are isolated here.

### The DOS memory warning

The comments explain a central restriction: a 320×240 RGB565 frame is 153,600 bytes, while a normal 16-bit DOS memory segment is only 65,536 bytes.

A 320×16 tile is 10,240 bytes, so it fits comfortably.

## `shared/src/gfx_color.h`

This file defines what one logical pixel is.

The normal shipping configuration uses:

```c
typedef uint16_t gfx_color_t;
```

That means one pixel is an unsigned 16-bit number in RGB565 format.

It also defines convenient color construction and constants such as black, white, red, green, blue, yellow, cyan, and magenta.

### Why DOS still uses RGB565 logically

The shared game and renderer use the same logical colors everywhere. DOS performs its reduction to a VGA-friendly palette only at the final presentation boundary.

This is important because it means the gameplay and renderer do not need a DOS-specific color version.

### Optional INDEX8 mode

There is also an optional 8-bit compatibility mode used for testing older assumptions. It is not the normal shipping path.

## `shared/src/gfx_fixed.h`

This file implements **fixed-point arithmetic**.

A computer normally represents fractional values using floating-point numbers such as `1.5`. Old or small systems may perform floating-point calculations slowly or inconsistently.

Fixed-point arithmetic stores a fraction inside an integer.

A simple analogy is money:

- instead of storing `$1.25` as a floating-point value,
- store `125 cents` as an integer,
- and remember that the last two digits are fractional.

MicroRender normally uses 16.16 fixed point:

- 16 bits for the whole-number portion,
- 16 bits for the fractional portion.

It can use a lighter 8.8 style on old compilers where 64-bit intermediate multiplication is undesirable.

## `shared/src/gfx.h`

This is the renderer’s public contract.

A header file mostly answers:

> “What data exists, and what functions may other files call?”

### `gfx_renderer_t`

This structure describes the current rendering job:

- total screen width and height,
- current tile’s location and size,
- tile-buffer pointer,
- tile-buffer capacity,
- current clipping rectangle,
- flush callbacks,
- platform-specific user data.

It does **not** own or allocate memory. The caller supplies the buffer.

### Flush callbacks

The renderer supports:

- a normal synchronous flush,
- an asynchronous `begin` function,
- an asynchronous `wait` function.

Synchronous means:

> “Send these pixels and do not return until finished.”

Asynchronous means:

> “Start sending these pixels, return immediately, and I will ask later whether the transfer is finished.”

That second form enables Pico DMA pipelining.

### `gfx_sprite_t`

A sprite is a small image, usually a character, item, or object.

The sprite structure stores:

- width and height,
- pixel data,
- optional RLE runs,
- optional row-start lookup data,
- transparent key color,
- flags describing the storage format.

### `gfx_tilemap_t`

A tilemap builds a larger world from a grid of small reusable images.

Instead of storing a unique picture for every part of a 1024×1024 world, the program can store:

- a small tileset,
- a grid of tile numbers.

For example:

```text
0 0 0 1 1 1
0 2 2 2 3 1
0 2 4 2 3 1
```

Each number means “draw this tileset image here.”

### Rectangle structures

`gfx_rect_t` represents a rectangle using `x`, `y`, `w`, and `h`.

Rectangle helpers are used for:

- clipping,
- collision,
- sprite bounds,
- dirty-region merging,
- screen-boundary checks.

### Dirty lists

A dirty list records areas of the screen that changed.

If a character moves, both of these areas need repainting:

- where the character used to be,
- where the character is now.

The renderer can merge overlapping or touching rectangles and redraw only those regions.

### Statistics

`gfx_blit_stats_t` counts work such as:

- sprites considered,
- sprites rejected because they are offscreen,
- pixels tested,
- pixels copied,
- RLE runs processed.

Those numbers help explain performance rather than merely reporting FPS.

## `shared/src/gfx.c`

This is the main software renderer implementation.

It is one of the most important files in the repository.

### Initialization

`gfx_init()` records the screen, buffer, tile height, flush callback, and user data.

It does not allocate anything.

### Beginning a tile

`gfx_begin_tile()` or `gfx_begin_tile_rect()` says:

> “The buffer now represents this rectangle of the screen.”

The function also resets clipping and checks that the requested tile cannot exceed the buffer’s capacity.

### Clipping

Clipping means refusing to draw outside an allowed rectangle.

Suppose a sprite is partly outside the left side of the screen. A naive renderer might try to write memory before the buffer and crash or corrupt data.

The clipped path calculates which columns and rows are actually visible and draws only those.

### Primitive drawing

The file implements basic shapes:

- one pixel,
- horizontal line,
- vertical line,
- filled rectangle,
- rectangle outline,
- general line.

More complicated visuals are built on top of simple operations.

### Sprite drawing paths

There are several ways to draw a sprite because different data layouts have different costs.

#### Raw opaque sprite

Every pixel is visible.

The renderer can copy a row quickly, often using a memory-copy operation.

#### Color-key sprite

One chosen color means “transparent.”

The renderer checks each pixel:

```text
Is this the transparent key?
- yes: skip it
- no: copy it
```

#### RLE sprite

RLE means **run-length encoding**.

Imagine a row like this, where dots are transparent:

```text
......XXXXX....XXX.......
```

Instead of storing and checking every dot, RLE can store roughly:

```text
start at x=6, copy 5 pixels
start at x=15, copy 3 pixels
```

That is useful when a sprite contains large transparent areas.

#### Row-indexed RLE

Basic RLE can still waste time searching all runs to find the ones belonging to the current rendering strip.

The row-start index is a table of contents:

```text
row 0 runs begin here
row 1 runs begin here
row 2 runs begin here
...
```

The renderer can jump directly to the relevant runs instead of scanning from the beginning.

This is one of the project’s most important optimizations.

### RLE validation

Fast drawing code assumes its data is valid.

When RLE data comes from a file, `gfx_sprite_rle_validate()` checks it once during loading. This prevents malformed runs from pointing outside sprite bounds or pixel pools.

The hot path then avoids repeating those checks every frame.

### Tilemap drawing

`gfx_draw_tilemap()` determines:

- which map cells are visible through the camera,
- which artwork tile each cell references,
- where that tile belongs on screen,
- whether it intersects the current rendering strip.

It avoids drawing the entire world when only a 320×240 window is visible.

### Dirty rectangles

The file contains rectangle operations and dirty-list logic:

- clip rectangles to the screen,
- intersect them,
- combine them,
- detect overlap,
- add changed areas,
- merge nearby regions,
- switch to a full redraw when many small rectangles become less efficient.

### Tiled rendering functions

The rendering wrappers repeatedly:

1. begin a tile,
2. optionally clear it,
3. call the scene-drawing callback,
4. flush the result.

There are variants for:

- a full screen,
- a sub-region,
- no automatic clear,
- asynchronous two-buffer pipelining.

## `shared/src/gfx_triangle.c`

This file draws filled triangles.

A triangle is sorted from top to bottom. The code then walks down the triangle one horizontal scanline at a time, calculates the left and right edge positions, and fills the span between them.

This is called **scanline rasterization**.

The stress test can enable triangles to add another type of workload.

## `shared/src/gfx_font5x7.c` and `.h`

These files contain a tiny built-in bitmap font.

Each character is represented by a grid roughly five pixels wide and seven pixels tall.

The font is used for:

- FPS counters,
- debug information,
- stress metrics,
- messages,
- title and status overlays.

A built-in font avoids depending on an operating-system font system, which DOS and Pico do not provide in the same way as a desktop.

## `shared/src/mr_strbuf.c` and `.h`

These files build text safely inside fixed-size character buffers.

They append:

- one character,
- a string,
- an unsigned number,
- a signed number,
- a fractional value.

### Why not just use `sprintf`?

`sprintf` can be larger, less predictable, unsafe when the destination size is not enforced, and troublesome across old and new compilers.

The bounded string builder:

- never writes beyond the supplied end pointer,
- works on MSVC, GCC, Clang, ARM GCC, and Open Watcom,
- avoids pulling unnecessary formatting machinery into small targets.

---

# Part 6: the shared game-and-engine layer

## `shared/src/gfx_engine.h` and `.c`

This is a small game-oriented layer built above the renderer.

The renderer knows how to draw pixels. The engine layer knows about moving objects, animation, collision maps, and cameras.

## Actors

An **actor** is a game object that can have:

- a world position,
- velocity,
- a sprite,
- animation state,
- collision bounds,
- movement limits,
- visibility and behavior flags.

The player and enemies are actors.

## Animation

An animation is a timed sequence of sprite frames.

The engine supports normal looping and ping-pong behavior. Ping-pong means frames play forward and then backward rather than jumping directly from the last frame to the first.

## Collision map

The world is divided into grid cells. A collision flag says whether a cell is solid.

The game does not inspect the artwork’s color to decide whether something is a wall. It uses a separate logical collision map.

This is cleaner because artwork and gameplay meaning are different things.

## Movement resolution

Movement is resolved along X and Y separately.

Conceptually:

1. Try moving horizontally.
2. Stop at a wall if necessary.
3. Try moving vertically.
4. Stop at a wall if necessary.

This makes axis-aligned tile collision predictable and prevents the player from passing through solid cells.

The code also steps carefully enough that a large movement does not jump completely through a thin wall. That problem is commonly called **tunneling**.

## Camera

The camera is the moving window through which the larger world is viewed.

It supports:

- following a target,
- a dead zone so the camera does not twitch for every tiny movement,
- clamping so the view does not leave the world,
- temporary screen shake.

Actor world coordinates are converted to screen coordinates by subtracting the camera position.

## `shared/src/mr_demo_input.h`

This header defines the platform-neutral control structure.

It is intentionally simple. The shared game asks only what the player intends to do, not which physical key or wire produced the action.

## `shared/src/mr_autodemo.c` and `.h`

The automatic demo generates deterministic input.

It can:

- start the game,
- walk in repeatable directions,
- exercise the scene without a person pressing controls.

This is useful for:

- screenshots,
- recorded demonstrations,
- CI frontend execution,
- comparable performance runs,
- verifying that a target is visibly alive.

Because the pattern is deterministic, two runs can be compared more fairly.

## `shared/src/mr_game_demo.h`

This header defines the complete shared demonstration game state.

Important constants include:

- 320×240 default screen,
- 16×16 artwork tiles,
- 64×64 map cells,
- up to 6 actors,
- up to 10 pickups,
- up to 48 particles,
- a 70 Hz simulation tick.

The structure contains all game state:

- map and collision arrays,
- tile graphics,
- player and enemy sprites,
- animation descriptions,
- actors,
- pickups,
- triggers,
- particles,
- camera,
- counters,
- messages,
- title/pause/debug flags.

There is no hidden object system. The state is explicit and statically sized, which suits DOS and microcontroller targets.

## `shared/src/mr_game_demo.c`

This is the playable demonstration shared by all frontends.

### Procedural graphics

The demo currently creates several simple graphics in code:

- map tiles,
- player frames,
- enemy sprite,
- pickup sprite.

That makes the core demo self-contained even before external art is loaded.

### World generation

The map and world objects are built in predictable positions.

The latest gameplay rules distinguish:

- grey blocks: solid walls,
- blue terrain: walkable but slower,
- normal terrain: walkable at normal speed.

Pickups are placed on verified walkable cells.

### Initialization

`mr_game_demo_init()` prepares every subsystem and starts the scene in a known state.

### Tick/update

`mr_game_demo_tick()` performs one fixed simulation step.

It handles:

- title, pause, debug, and quit controls,
- player direction,
- terrain-dependent speed,
- movement and wall collision,
- player animation,
- enemy movement,
- enemy contact and restart,
- pickup collection,
- trigger messages,
- particles,
- camera movement and shake,
- event counters.

### Enemy restart behavior

When the player touches an enemy:

- the game restarts,
- pickups return to their initial untaken state,
- the restart counter changes,
- an impact event can create visual feedback.

Tests verify this behavior.

### Particles

Particles are tiny short-lived visual objects used for collection and impact effects.

Their positions and velocities are stored in eighths of a pixel using integers. This permits smoother-than-one-pixel motion while remaining deterministic and avoiding floating-point differences.

### Rendering

`mr_game_demo_render()` draws the current state through the common renderer.

It does not know whether the result is headed to DOS, Pico, or Raylib.

## `shared/src/mr_stress_test.h` and `.c`

The stress test is not primarily a game. It is a controlled workload designed to make the renderer work hard.

It can create up to 1024 moving sprites over a scrolling map while also exercising:

- RLE drawing,
- visibility rejection,
- collision checks,
- triangle drawing,
- text/HUD rendering,
- sprite bucketing,
- different presentation strategies.

## Why the stress test is separate

A real game’s workload changes with level design. A synthetic test can hold important variables steady.

This lets the developer ask useful questions such as:

- Does row-indexed RLE beat color-key drawing here?
- How much does tile height matter?
- Is the CPU or LCD transfer the bottleneck?
- How many sprites were visible rather than merely requested?
- Does dirty-rectangle presentation help this scene?

## Sprite buckets

With many sprites, checking all 1024 sprites for every 16-row rendering band would repeat a lot of work.

The stress system can group sprite indices into vertical bands in advance. When rendering a band, it visits only sprites assigned to that band.

This is a spatial organization optimization: do not ask every object whether it matters when a simple table can narrow the candidates first.

---

# Part 7: the asset pipeline

## Why assets need a pipeline

Artists and level tools produce convenient source files such as:

- BMP images,
- JSON metadata,
- tilemap exports,
- WAV audio.

The game should not contain a large general-purpose image editor or JSON parser. Instead, desktop tools convert the source assets into a compact package before the game runs.

## `shared/assets/project.json`

This is the asset manifest.

A manifest is a list describing:

- which source files belong to the project,
- what each file represents,
- how it should be converted,
- identifiers used by the runtime.

## `shared/tools/mr_pack.py`

This Python tool compiles source assets into `GAME.MRP`.

The pack can hold several resource kinds, including:

- raw sprites,
- RLE sprites,
- tilemaps,
- palettes,
- animation data,
- collision data,
- spawn points,
- triggers,
- tile flags,
- audio,
- project information.

The tool understands source formats on the development computer and emits a simple little-endian runtime format.

### Why little-endian?

Little-endian describes how multi-byte numbers are ordered in a file. The selected format matches the target machines and keeps the runtime reader straightforward.

## `shared/src/gfx_pack.h` and `.c`

This is the small runtime pack reader, especially useful on DOS.

It can:

- open a package,
- verify its header,
- inspect directory entries,
- find an asset by identifier or kind,
- read asset bytes,
- close the file.

The reader is deliberately small. Expensive conversion work belongs in the desktop packing tool, not in the game.

## `shared/tools/mr_embed.py`

DOS can read `GAME.MRP` from a filesystem.

A Pico firmware normally does not use the same disk-file model. `mr_embed.py` converts the exact package bytes into C source and header files containing a byte array.

That array is compiled directly into the Pico firmware.

The important point is:

> Both platforms use the same packed data; they merely obtain the bytes differently.

## `shared/generated/`

This directory contains generated results such as:

- `GAME.MRP`,
- embedded C data,
- embedded-data declarations.

These files should not be manually edited. Change the source assets or tools and regenerate them.

## `shared/tools/mr_asset.c`

This is an additional native asset/conversion utility used by the project’s tooling experiments and workflows.

It demonstrates that asset processing does not have to be part of the runtime renderer.

---

# Part 8: the Pico 2 frontend

## What the Pico must do

The Pico version has to:

1. start the RP2350 hardware,
2. configure clocks,
3. configure USB stdio when needed,
4. configure SPI and LCD pins,
5. initialize the ILI9341 panel,
6. allocate or select rendering buffers,
7. run the shared game or stress test,
8. send pixels to the LCD,
9. optionally answer screenshot commands.

The shared renderer does not perform these hardware jobs.

## `microrender/CMakeLists.txt`

CMake is a program that creates build instructions for a compiler.

This file describes:

- which source files form the Pico executable,
- which Pico SDK libraries are required,
- screen size,
- LCD pins,
- SPI speed,
- system clock,
- game or stress application selection,
- presentation mode,
- screenshot support,
- compile-time validation,
- generated-asset steps.

### Why configuration lives here

Instead of editing C source every time the developer wants a different test, the build can pass options such as:

```text
MR_APP=GAME
MR_STRESS_MODE=lace
MR_STRESS_SPRITES=1024
MR_PICO_SYS_KHZ=300000
MR_LCD_SPI_BAUD=75000000
```

The source remains stable while the build recipe changes.

## `microrender/CMakePresets.json`

A preset is a named recipe containing common CMake options and a build directory.

Examples include:

- `game`,
- `game-raw`,
- `stress-visible`,
- `stress-raw`,
- `stress-lace`,
- `stress-render`,
- `stress-dirtyrect`.

Presets make repeatable tests easier and prevent long command lines from being retyped incorrectly.

## `microrender/microrender.c`

This is intentionally tiny.

It is the Pico program’s top-level entry point. It initializes the necessary standard I/O environment and calls either:

- the shared-game Pico frontend,
- or the stress-test Pico frontend.

A small entry point is evidence that most behavior lives in reusable modules rather than being tangled in `main()`.

## `microrender/pico_sdk_import.cmake`

This is the standard bridge used to locate and initialize the Raspberry Pi Pico SDK from CMake.

## `microrender/pico_env_auto.bat`

This Windows helper locates the tools installed by the Raspberry Pi Pico VS Code extension, including:

- Pico SDK,
- ARM GCC toolchain,
- Ninja.

It fills environment variables when they were not already supplied explicitly.

## `.vscode/` files

These files configure the editor’s Pico experience:

- compiler include paths,
- recommended extensions,
- CMake kits,
- build tasks,
- launch/debug information,
- project settings.

They do not contain the renderer. They help VS Code understand how to build, flash, and debug it.

## `shared/rp2350/mr_pico_ili9341.h` and `.c`

These files are the LCD driver.

### ILI9341

The ILI9341 is the controller chip behind the 320×240 LCD.

The Pico communicates with it using SPI.

### SPI

SPI is a serial communication method. The Pico sends a stream of bits over wires including clock, data, chip-select, and command/data control.

### Panel initialization

The driver:

- configures GPIO pins,
- resets the panel,
- sends controller commands,
- selects RGB565 pixel format,
- chooses orientation,
- sets drawing windows.

### Drawing window

Before pixel data is sent, the LCD is told which rectangular area will receive it.

That matches MicroRender’s flush rectangle perfectly.

### DMA

DMA means **Direct Memory Access**.

Normally, the CPU might manually send each piece of data. DMA allows a hardware engine to move a block of pixels from memory to SPI with much less CPU involvement.

The driver exposes:

- begin transfer,
- wait for transfer completion.

Those map directly to the renderer’s asynchronous flush interface.

## `shared/rp2350/mr_pico_demo.c`

This is the Pico adapter for the shared game.

It:

- configures the system and LCD,
- creates renderer buffers,
- initializes the shared game,
- translates Pico input or autodemo input,
- runs the fixed game update loop,
- renders using raw or pipelined presentation,
- tracks FPS,
- polls the shared screenshot service.

The file’s scene callback simply asks the shared game to draw. That is the boundary between platform and game.

## `shared/rp2350/mr_pico_stress_demo.c`

This is the largest platform-specific experiment file because it compares several ways of presenting a heavy scene to a slow LCD link.

### Why presentation matters

The CPU may be able to calculate pixels faster than SPI can send all 153,600 bytes to the panel.

A high renderer-only FPS does not automatically mean the physical display can receive complete new frames at that rate.

There is a third limit past those two. Even when the renderer is fast enough and the link can carry the bytes, the panel scans its own memory at its own fixed rate — around 70 Hz for an ILI9341 at reset defaults. Frames delivered faster than that are overwritten before they are displayed. All three limits are real, and on this hardware the panel is the lowest of them.

The modes separate those costs.

### `visible`

Render the scene and visibly send the complete result.

This is the straightforward optimized visible baseline.

### `raw`

Clear and draw the complete logical frame, then synchronously send the whole frame.

This is deliberately simple and easy to understand. It is useful as a baseline, not necessarily the fastest design.

### `render`

Measure renderer work while minimizing or suppressing repeated LCD presentation after an initial proof frame.

This helps answer:

> “How fast is the CPU-side renderer if the LCD transfer is not the main limit?”

### `everyN`

Render repeatedly but update the LCD only every Nth frame.

The simulation and renderer can run faster while the display receives fewer updates.

### `dirty`

Update selected changed row spans rather than always sending the whole display.

### `dirtyrect`

Compare the newly rendered frame with a previous frame, find changed pixels, combine changed areas into rectangles, and upload those rectangles.

This can be excellent when little changes and poor when most of the screen changes.

The implementation therefore includes thresholds and a fallback to a full transfer.

### `lace`

Update alternating groups of rows on different frames.

For example, one frame may update one set of row bands, and the next frame updates the other set.

This reduces the number of bytes sent per displayed update, increasing apparent update frequency. The tradeoff is that some rows temporarily contain pixels from the previous moment, which can create combing or temporal artifacts during motion.

The word “lace” is related to interlaced-style presentation, but this is a custom row-group strategy rather than a claim of standard television interlacing.

#### Presenting from the second core

Sending a row group is mostly waiting. Each group needs its own window command written to the panel, then a DMA transfer, then a wait for the transfer to drain before the next command can be sent. On a single core, that waiting happens instead of rendering: a measured frame spent about 5.2 ms rasterizing and about 9 ms transferring, strictly one after the other.

`MR_PICO_PRESENT_CORE1` moves the sending to the RP2350's second core. Core 1 owns the panel for a whole frame while core 0 renders the next one into a different buffer, so the two overlap and the frame costs roughly the longer of the two rather than their sum. Measured on a Pimoroni Pico Plus 2, this took the 1,024-sprite scene from about 77 FPS to 110.8 FPS with no change to clocks, pixel format, or what is drawn.

The cost is a second 150 KiB frame buffer, because core 0 must not be writing into the buffer core 1 is reading.

Two honest qualifications, because this is exactly the kind of number that invites over-reading:

The stress test advances its simulation one step per frame and takes no delta time, so sprites move about 45% faster at 110 FPS than at 77. Side by side, the faster build looks smoother mostly because the simulation is running quicker. That is a property of the benchmark, not evidence about the renderer.

The panel scans its own memory near 70 Hz regardless. Presenting 110 frames per second into a 70 Hz panel means many of them are overwritten before they are ever displayed. The observed visual difference between 77 and 110 FPS was small — no change in the background, and possibly slightly less jitter.

So 110.8 FPS is a real throughput result and the right thing for a stress test to demonstrate, but it is throughput, not a promise about what a person sees.

### `lcdtest`

Send simple full-screen patterns to measure the LCD/DMA path itself.

This helps determine whether the bottleneck is:

- game simulation,
- software rendering,
- memory copying,
- SPI transfer,
- or panel behavior.

### Fixed-camera variants

Some modes can hold the camera still. This makes dirty-region strategies more favorable because a scrolling background otherwise changes nearly every pixel.

## `shared/rp2350/mr_pico_screenshot.h` and `.c`

This is the universal Pico screenshot service.

It is intentionally independent of one particular demo.

A Pico app registers:

- logical width and height,
- a reusable tile buffer,
- a scene-drawing callback,
- an optional function that waits for active display DMA.

The service polls USB input for commands such as:

```text
SCREENSHOT
SHOT
PING
HELP
```

When a screenshot is requested, it:

1. waits until the display is no longer using the shared buffer,
2. sends an `MRSHOT1` header,
3. renders a clean logical image tile by tile,
4. streams little-endian RGB565 bytes over USB.

### Why it renders a clean frame

In lace mode, the physical LCD can temporarily contain rows from two different moments. The screenshot service renders the complete logical scene on demand so the PNG represents the game frame, not a photograph of that temporary mixed panel state.

### Why it does not allocate another full framebuffer

The service reuses the application’s rendering buffer and streams tiles. That preserves the project’s memory-conscious architecture.

## `microrender/tools_capture_pico_screenshot.py`

This is the desktop half of the screenshot system.

It:

1. opens a serial port such as `COM5`,
2. sends `SCREENSHOT`,
3. waits for the `MRSHOT1` header,
4. reads the exact RGB565 byte count,
5. converts each RGB565 pixel into normal image color,
6. saves a PNG.

The tool includes timeouts and diagnostics so unsupported firmware or a busy serial port does not look like an unexplained permanent hang.

---

# Part 9: the DOS frontend

## Why DOS is unusual

The DOS target is not merely a slow desktop build.

It has several old-machine constraints:

- 16-bit integers,
- segmented memory,
- far pointers,
- VGA hardware planes,
- no modern windowing API,
- no standard real-time held-key API,
- Open Watcom compiler behavior.

The goal is still to keep those details outside the shared renderer and game.

## `microrender_dos/build_watcom.bat`

This script compiles the DOS game using Open Watcom.

It compiles:

- shared renderer files,
- shared game files,
- bounded string formatting,
- DOS frontend files.

It links different executables for optimized tiled and raw variants.

The recent `mr_strbuf.c` addition is important because the game HUD now uses the portable string builder.

## `microrender_dos/build_watcom_stress.bat`

This performs the corresponding build for the DOS stress test.

## `microrender_dos/dos/dos_main.c`

This is a tiny conventional `main()` that forwards to the DOS application implementation.

## `microrender_dos/dos/dos_app.c`

This is the DOS adapter for the shared game.

It owns DOS-only responsibilities:

- command-line options,
- video-mode entry and restoration,
- keyboard input translation,
- timing,
- tiled or raw presentation selection,
- autoplay,
- FPS reporting.

It calls the same `mr_game_demo_tick()` and `mr_game_demo_render()` used elsewhere.

### Reliable cleanup

Old graphics programs can leave the machine in an inconvenient video mode if they exit badly. The DOS frontend registers cleanup so text mode and keyboard state are restored.

## `microrender_dos/dos/dos_keyboard.h` and `.c`

The BIOS keyboard queue is useful for typed characters, but games need immediate held-key state.

This code installs an interrupt handler for hardware keyboard interrupt 9.

It tracks whether scan codes are currently pressed or released, allowing combinations and continuous movement.

The original interrupt handler is restored when the program exits.

## `microrender_dos/dos/dos_vga.h` and `.c`

This is the VGA presentation driver.

### Mode X

The code configures an unchained planar 320×240 VGA mode commonly called Mode X.

### Planar memory

Modern images are usually thought of as consecutive pixels. VGA Mode X divides pixels among four memory planes.

The driver selects a plane and writes the pixels belonging to that plane.

### RGB565 to RGB332 conversion

The shared renderer produces RGB565 pixels.

The DOS display path uses a 256-color palette. A lookup table converts each 16-bit logical color to an 8-bit RGB332-style palette index during presentation.

This keeps color reduction out of the game and renderer.

### Vertical blank

Optional vertical-blank waiting can reduce visible tearing by synchronizing updates with the display’s refresh cycle.

### Ticks

The driver also exposes DOS timing used for frame and FPS measurements.

## `microrender_dos/dos/dos_stress_app.c`

This wraps the shared stress test in DOS-specific setup, timing, options, and output.

It can change sprite count and frame duration from the command line for repeatable DOSBox measurements.

## DOS recording and helper scripts

Files such as:

- `start_watcom_here.bat`,
- `show_bench_csv.bat`,
- `tools/record_dosbox_window_ffmpeg.ps1`,

help establish the compiler environment, inspect benchmark output, and capture footage. They are workflow tools, not renderer logic.

---

# Part 10: the Raylib desktop frontend

## Why a desktop frontend matters

A modern window is much easier to inspect than repeatedly flashing a Pico or launching DOSBox.

The Raylib frontend provides:

- fast visual iteration,
- keyboard input,
- a reference presentation implementation,
- headless CI testing through a stub,
- comparison of raw, tiled, lace, and dirty-rectangle behavior.

## `microrender_raylib/CMakeLists.txt`

This CMake file builds the desktop program and links Raylib.

The normal project setup prefers the pinned Raylib submodule, while still permitting an explicit external path when needed.

It compiles the same shared renderer, game, and stress sources.

## `microrender_raylib/main.c`

This is the desktop platform adapter.

### Texture

A Raylib texture represents the image shown in the window.

The frontend uses an RGB565-compatible texture format and nearest-neighbor scaling so one logical pixel remains a sharp block rather than becoming blurry.

### Input translation

Raylib key queries are converted into `mr_demo_input_t`, just like DOS scan codes or Pico controls.

### Presentation modes

The desktop frontend can demonstrate:

- raw full-frame upload,
- tiled region upload,
- lace row-group upload,
- dirty-rectangle upload.

That makes presentation algorithms visible without requiring the physical LCD.

### Integer scaling

The 320×240 logical image can be enlarged by a whole-number scale while preserving the intended pixel-art appearance.

---

# Part 11: build and command files

## `mr.bat`

This is the user-facing command entry point.

Instead of remembering separate scripts, the developer can use commands such as:

```text
mr.bat build assets
mr.bat build tests
mr.bat build dos
mr.bat build pico game
mr.bat build raylib
mr.bat build all
mr.bat test
mr.bat bench
mr.bat clean
```

It delegates the real work to scripts in `scripts/`.

## `scripts/mr_build.bat`

This is the main Windows build orchestrator.

It handles:

- regenerating assets,
- configuring and compiling host tests,
- locating and invoking Open Watcom,
- configuring each Pico preset with Ninja and ARM GCC,
- translating friendly `key=value` options into CMake definitions,
- detecting stale CMake caches after the repository moves,
- initializing the Raylib submodule,
- building the Raylib frontend,
- continuing through all targets while remembering whether any failed.

### Stale CMake caches

CMake caches absolute source and build paths. If a repository moves from one folder to another, reusing the old cache can produce confusing errors.

The script checks stored paths, generator, and compiler and removes incompatible build directories before reconfiguration.

### Option parsing

Windows batch argument parsing has surprising behavior around unquoted equals signs. The current parser supports project options while validating unknown or empty values rather than silently creating invalid preprocessor definitions.

## `scripts/mr_tools.bat`

This locates required tools such as CMake, Open Watcom, and DOSBox.

Keeping discovery in one helper avoids repeating it in every command.

## `scripts/mr_run.bat`

This launches built programs and tests.

It can:

- execute host tests,
- run the benchmark,
- start Raylib,
- create a temporary DOSBox configuration,
- run DOS binaries with selected cycle settings.

## `scripts/mr_clean.bat`

This removes generated build directories without deleting source files.

## `scripts/mr_preset_flags.py`

This helper translates or inspects preset-related settings used by the build workflow, keeping complicated argument derivation out of the batch file where practical.

## `third_party/raylib/`

Raylib is stored as a Git submodule.

A submodule records a particular commit of another Git repository rather than copying its source history into this repository.

Pinning the dependency helps every developer and CI job build against the same version.

---

# Part 12: testing and CI

## Why low-level graphics needs aggressive tests

Drawing bugs often appear only at awkward boundaries:

- one pixel off the left edge,
- exactly on a tile seam,
- a sprite completely offscreen,
- a negative world coordinate,
- a malformed RLE run,
- a dirty rectangle touching another by one pixel.

A scene that “looks okay” during one play session is not enough evidence.

## `tests/CMakeLists.txt`

This creates host executables for:

- unit tests,
- shared-game behavior tests,
- fuzz tests,
- benchmark tests.

The shared core can be compiled on a desktop without Pico SDK, DOSBox, or Open Watcom.

That is a major benefit of the platform boundary.

## `tests/mr_test_support.h`

This contains common test helpers and framebuffer/flush support used by multiple tests.

A fake flush callback can inspect every rectangle the renderer produces.

## `tests/mr_test_unit.c`

These are focused correctness tests.

They cover areas such as:

- raw sprite drawing,
- transparent color-key drawing,
- RLE drawing,
- row-indexed RLE drawing,
- equality between multiple sprite paths,
- clipping at screen edges,
- behavior at tile seams,
- rectangle operations,
- dirty-list merging,
- tile-buffer capacity,
- malformed RLE rejection,
- line endpoints,
- negative collision coordinates,
- string-buffer formatting.

A particularly useful strategy is to draw the same sprite through several implementations at many difficult positions and compare the resulting framebuffers byte for byte.

## `tests/mr_test_game.c`

These tests check game rules rather than isolated drawing functions.

They verify matters such as:

- expected screen and camera setup,
- actors remaining in valid bounds,
- pickup collection creating effects,
- enemy collision restarting the game,
- pickups resetting after restart,
- player movement occurring only once per tick,
- blue terrain being walkable but slower,
- grey blocks being solid,
- pickups starting on walkable tiles.

## `tests/mr_test_fuzz.c`

Fuzz testing feeds drawing functions many unusual but deterministic inputs.

Coordinates may range far outside the screen, for example from -4000 to +4000.

It exercises:

- randomized clips,
- hostile sprite positions,
- tiled and sub-region rendering,
- pipelined paths,
- dirty rectangles,
- malformed RLE data.

The flush callback independently checks that the renderer never hands it an invalid rectangle.

This is like repeatedly shaking the program to see what falls loose.

## `tests/mr_test_bench.c`

The benchmark compares implementation choices on the host.

It can measure:

- raw opaque copying,
- color-key transparency,
- linear RLE,
- row-indexed RLE,
- different rendering tile heights.

These are comparative renderer measurements, not promises of period DOS hardware or physical Pico LCD FPS.

They are also host measurements, which is a sharper caveat than it sounds. A modern compiler will auto-vectorize the simple pixel loops into SIMD instructions that no target this project runs on actually has, so a change can look slower on the host and faster on a Cortex-M33, or the reverse. Treat the host benchmark as a way to compare algorithms, and measure the target when the question is about the target.

## `tests/raylib_stub/`

The Raylib stub is a tiny fake implementation of the Raylib functions the frontend uses.

It allows CI to compile and execute the frontend without opening a real graphical window.

This checks that:

- command-line modes initialize,
- shared loops run,
- the frontend calls legal interfaces,
- the program exits successfully.

It is not a visual quality test.

## `.github/workflows/ci.yml`

CI means **continuous integration**.

GitHub runs automated jobs after code changes.

The workflow covers combinations such as:

- Linux with GCC and sanitizers,
- macOS with Clang and sanitizers,
- Windows with Visual Studio/MSVC,
- unit and game tests,
- deterministic fuzz seeds,
- headless Raylib frontend modes,
- deterministic asset packing,
- informational benchmarks.

### Sanitizers

AddressSanitizer looks for invalid memory access.

UndefinedBehaviorSanitizer looks for operations the C language says are invalid or unpredictable.

They are especially valuable in a pointer-heavy software renderer.

---

# Part 13: follow one keypress through the entire project

Let us trace the right-arrow key.

## On Raylib

1. Raylib reports that the right key is held.
2. `microrender_raylib/main.c` sets the common input’s right/direction field.
3. `mr_game_demo_tick()` receives the input.
4. The game examines the terrain under or around the player.
5. It chooses normal speed or slowdown speed.
6. The actor engine attempts horizontal movement.
7. The collision map checks cells along the actor’s bounds.
8. If a solid grey tile blocks the route, movement stops at the boundary.
9. Otherwise the actor’s world position changes.
10. Animation state updates.
11. The camera follows the player within its rules.
12. Rendering converts world position to screen position.
13. The sprite is drawn into each rendering strip it intersects.
14. Raylib uploads the pixels to its texture.

## On DOS

The middle of that journey is identical.

Only the beginning and end change:

- keyboard interrupt state creates the common input,
- VGA code presents the logical RGB565 result as planar palette pixels.

## On Pico

Again, the shared middle is identical.

- Pico controls or autodemo create the common input,
- the ILI9341 driver sends pixels over SPI DMA.

That is portability in concrete terms: the key source and display destination change, but the game decision and drawing logic do not.

---

# Part 14: follow one pixel through the entire project

Suppose part of the player sprite contains a magenta pixel.

1. The sprite stores that pixel as a 16-bit RGB565 value.
2. The game decides the player belongs at a world position.
3. The camera converts that world position into screen coordinates.
4. The renderer begins a horizontal screen strip.
5. The blitter checks whether the sprite overlaps that strip and the clip rectangle.
6. The blitter finds the source pixel.
7. If the sprite uses transparency, it checks whether the pixel is the transparent key.
8. If visible, it writes the RGB565 value into the correct tile-buffer location.
9. When the strip is complete, the renderer flushes it.
10. On Pico, the value is sent to the LCD as RGB565.
11. On Raylib, the value is uploaded into an RGB565 texture.
12. On DOS, the value is looked up and converted to an 8-bit palette index, then written to the proper VGA plane.

The logical pixel remains platform-independent until the last responsible moment.

---

# Part 15: the presentation modes compared without jargon

| Mode | Beginner analogy | Advantage | Tradeoff |
|---|---|---|---|
| Raw | Paint the whole picture, then carry the whole picture to the display | Extremely simple baseline | Uses a full buffer and sends everything |
| Tiled | Paint and deliver one horizontal strip at a time | Low memory and portable | Repeats scene traversal per strip |
| Pipelined tiled | Use two trays so painting and delivery overlap | Hides some transfer waiting | Needs a second tile buffer and async support |
| Render-only | Paint but mostly do not deliver | Measures CPU-side renderer | Not representative of visible display rate |
| Every-N | Deliver only every few paintings | Higher simulation/render rate | Display updates less often |
| Dirty spans | Deliver changed horizontal portions | Saves bandwidth for local changes | Scrolling scenes can change too much |
| Dirty rectangles | Compare old/new paintings and deliver changed boxes | Excellent for sparse changes | Comparison/storage cost; poor for full motion |
| Lace | Deliver alternating row groups | Less data per visible update | Temporary old/new row mixture |
| LCD test | Send simple patterns repeatedly | Measures display path ceiling | Does not measure full game work |

No mode is universally “best.” The correct choice depends on what is changing, how much memory exists, and whether the bottleneck is CPU work or display transfer.

---

# Part 16: why this project is genuinely difficult

The project is hard for reasons that are mostly invisible in a screenshot.

## 1. The machines disagree about memory

A Pico has a flat modern address space. DOS uses segmented memory and 16-bit integer behavior.

The renderer must avoid assumptions that are harmless on one and broken on the other.

## 2. The machines disagree about displays

- Pico sends serial RGB565 to an LCD controller.
- DOS writes planar indexed VGA memory.
- Raylib updates a desktop texture.

The shared core still has to produce one coherent logical image model.

## 3. Performance is not one number

A slow frame can be caused by:

- game simulation,
- collision,
- sprite searching,
- pixel copying,
- RLE scanning,
- map drawing,
- memory layout,
- LCD bandwidth,
- synchronization,
- desktop emulation settings.

The stress modes separate those causes.

## 4. Optimizations can become regressions

An optimization may be faster in one case and slower in another.

For example:

- RLE saves transparent-pixel work,
- but naive RLE can repeatedly search runs,
- so the row-start index becomes essential.

Dirty rectangles help a mostly static screen but can lose when a scrolling background changes nearly everything.

## 5. Correctness must survive boundaries

Most renderer bugs occur at edges and seams, not in the center of the screen.

That is why the tests deliberately use negative coordinates, offscreen sprites, tile boundaries, malformed data, and randomized clips.

## 6. The build system is part of the product

A technically correct renderer is not useful if nobody can build it.

The scripts must coordinate:

- Visual Studio,
- GCC/Clang,
- ARM GCC,
- Ninja,
- Pico SDK,
- Open Watcom,
- DOSBox,
- Raylib,
- Python asset tools,
- moved repositories and stale caches.

That work is less glamorous than drawing pixels but essential for a real reusable project.

---

# Part 17: what a complete beginner should change first

Do not start by rewriting the VGA driver or DMA code.

Use this progression.

## First change: a color

Open `shared/src/mr_game_demo.c` and find where a procedural tile or sprite color is created.

Change one RGB value, rebuild Raylib, and observe the result.

This teaches:

- source edit,
- compilation,
- shared-code effect,
- RGB565 color construction.

## Second change: player speed

Find the normal and slowdown movement values in the game tick.

Change them and observe collision and animation behavior.

## Third change: a map cell

Change the procedural map generation so one tile becomes a wall or slow terrain.

Observe the difference between visual tile data and collision flags.

## Fourth change: a primitive

Add a rectangle or line in `mr_game_demo_render()`.

This shows how screen coordinates and clipping work.

## Fifth change: a test

Add a host test asserting the new behavior.

This teaches that a feature is not complete merely because it looked correct once.

## Sixth change: a Raylib-only presentation experiment

Try changing tile height or presentation mode on desktop before touching hardware-specific drivers.

## Last: platform drivers

After the shared flow is familiar, inspect:

- `mr_pico_ili9341.c`,
- `dos_vga.c`.

At that point the hardware code has context rather than appearing as unexplained register operations.

---

# Part 18: quick glossary

**Actor** — A game object with position, movement, sprite, and collision information.

**API** — The functions and data a module exposes for other modules to use.

**Asset** — Art, map, animation, audio, or other content used by the program.

**Buffer** — Reserved memory holding temporary data.

**Callback** — A function supplied to another module so it can call back when work is ready.

**Camera** — The movable view into a world larger than the screen.

**CI** — Automated builds and tests run when code changes.

**Clip rectangle** — The area inside which drawing is allowed.

**CMake** — A tool that generates compiler build instructions.

**Collision map** — A grid describing where movement is blocked.

**Compiler** — A program that translates C source into machine code.

**Deterministic** — Produces the same result from the same starting state and input.

**Dirty rectangle** — A screen region that changed and needs repainting.

**DMA** — Hardware that transfers memory data without requiring the CPU to move every item manually.

**Fixed point** — Integer-based representation of fractional values.

**Flush** — Send completed rendered pixels to the platform’s display destination.

**Frame** — One complete game image.

**Framebuffer** — Memory containing a complete screen image.

**Frontend** — The platform-specific outer layer that provides input, display, timing, and startup.

**Fuzz test** — A test that feeds many unusual inputs into code to find boundary errors.

**HUD** — Information drawn over the game, such as FPS or counters.

**ILI9341** — The LCD controller used by the Pico display.

**Little-endian** — A byte order used to store multi-byte numbers.

**Mode X** — An unchained planar VGA graphics mode used by the DOS frontend.

**Palette** — A table mapping small color indices to display colors.

**Pipeline** — Overlap stages of work, such as rendering one tile while another transfers.

**Pixel** — One colored screen square.

**Rasterize** — Convert shapes and images into pixels.

**Raylib** — A desktop programming library used here as a host frontend.

**Renderer** — Code that turns drawing commands into pixels.

**RGB565** — A 16-bit color format with 5 red, 6 green, and 5 blue bits.

**RLE** — Run-length encoding; stores consecutive visible pixel runs compactly.

**Scanline** — One horizontal row of pixels.

**SPI** — A serial hardware interface used to send data from Pico to LCD.

**Sprite** — A small 2D image placed into a scene.

**Submodule** — A Git repository pinned inside another Git repository.

**Texture** — An image object used by a graphics library or GPU.

**Tile buffer** — A reusable memory area holding one portion of the screen during rendering.

**Tilemap** — A world image assembled from a grid of reusable small artwork tiles.

**Toolchain** — Compiler, linker, build tools, and supporting programs used to create an executable.

**Undefined behavior** — A C operation whose result the language does not guarantee.

**World coordinates** — Object positions in the complete game world.

**Screen coordinates** — Positions relative to the current visible screen.

---

# Part 19: a condensed five-minute explanation

MicroRender is a 2D software renderer written in portable C. The same shared code draws a small game and a heavy stress scene on a Raspberry Pi Pico 2, a 16-bit DOS PC, and a Raylib desktop window.

The core problem is memory and display differences. A 320×240 RGB565 image is 153,600 bytes. That is manageable in some Pico modes but awkward for a normal 16-bit DOS object because of 64 KiB memory segments.

Instead of requiring a full framebuffer, the renderer accepts a caller-owned tile buffer—usually a horizontal strip. It draws only the scene portions intersecting that strip, then calls a platform-provided flush function. DOS flushes to planar VGA memory, Pico flushes over SPI DMA to an ILI9341 LCD, and Raylib flushes into a desktop texture.

The shared renderer implements pixels, lines, rectangles, triangles, sprites, transparency, RLE sprites, row-indexed RLE, tilemaps, clipping, dirty rectangles, and tiled or pipelined rendering. Above that, a small shared engine implements actors, animation, collision, and camera behavior. Above that sits a shared demonstration game and a separate stress test.

The game receives a platform-neutral input structure, so DOS keyboard scan codes, Raylib keys, and Pico controls all drive the same update function. The game’s map, collision, enemy restart, pickups, particles, slowdown terrain, camera, and HUD are therefore shared.

The Pico frontend initializes clocks and the LCD, sends tiles through DMA, compares several presentation strategies, and provides a universal USB screenshot service. The DOS frontend handles Mode X, planar VGA writes, RGB565-to-palette conversion, keyboard interrupt state, and segmented-memory-safe builds. Raylib provides a convenient modern reference window.

Assets are compiled into one `GAME.MRP` package. DOS reads it from disk, while Pico links the exact same bytes into firmware. Host tests compare drawing implementations pixel for pixel, fuzz dangerous coordinates and clips, verify game behavior, and exercise the frontend headlessly in CI.

The project is not hard because one rectangle is complicated. It is hard because the same rules must remain correct and fast across machines with different memory models, compilers, displays, input systems, and performance bottlenecks.

---

# Part 20: final takeaway

The most important lesson in MicroRender is not one assembly trick or one sprite optimization.

It is the architecture:

```text
shared simulation and drawing
            ↓
caller-owned pixel buffer
            ↓
platform-provided presentation callback
```

That design keeps the renderer testable on a modern host, small enough for old DOS constraints, and fast enough to experiment with Pico DMA and LCD bandwidth.

When somebody says, “I barely understood the first explanation,” the answer is not to remove the technical depth. The answer is to reveal the layers in the correct order:

1. A game updates state.
2. A renderer creates pixels.
3. It creates them in reusable strips when memory is tight.
4. A callback hands those pixels to each machine.
5. Everything platform-specific stays at the edges.
6. Tests prove that the shared middle still behaves the same.

Once that model is clear, the repository stops looking like dozens of unrelated C files. It becomes one pipeline with three different doors at the end.

---

# Appendix A: every tracked file in the reviewed tree

This appendix is the quick-reference answer to “What is this particular file for?” It includes documentation, build configuration, code, example assets, and tests. Generated build folders and initialized submodule contents are intentionally excluded.

## Repository root

| File | Plain-English purpose |
|---|---|
| `.gitattributes` | Tells Git how selected files should be treated, including line-ending behavior. |
| `.gitignore` | Lists generated files and build folders Git should not track. |
| `README.md` | Main project overview: purpose, architecture, performance, testing, assets, and basic builds. |
| `Building.md` | More detailed platform build prerequisites and commands. |
| `SCRIPTS.md` | Documents the `mr.bat` command system and its options. |
| `PICO_PRESENTATION_MODES.md` | Explains Pico display strategies and the tradeoffs between them. |
| `LICENSE` | MIT license granting permission to use, modify, and redistribute the project under its terms. |
| `mr.bat` | One convenient front door for building, running, testing, benchmarking, and cleaning. |

## GitHub automation

| File | Plain-English purpose |
|---|---|
| `.github/workflows/ci.yml` | Describes the automated Linux, macOS, Windows, sanitizer, frontend, asset, and benchmark jobs run by GitHub. |

## Pico project and editor support

| File | Plain-English purpose |
|---|---|
| `microrender/.gitignore` | Ignores Pico-specific generated build results. |
| `microrender/CMakeLists.txt` | Defines the Pico executable, sources, hardware options, presentation modes, validation, and generated assets. |
| `microrender/CMakePresets.json` | Named Pico build recipes and their separate build directories. |
| `microrender/microrender.c` | Small Pico entry point choosing the game or stress frontend. |
| `microrender/pico_sdk_import.cmake` | Imports the official Pico SDK into the CMake project. |
| `microrender/pico_env_auto.bat` | Finds the SDK, ARM compiler, and Ninja on Windows. |
| `microrender/tools_capture_pico_screenshot.py` | Requests a logical RGB565 frame over USB serial and writes a PNG. |
| `microrender/pico2_screenshot.png` | A captured example image produced by the screenshot workflow. |
| `microrender/.vscode/extensions.json` | Recommends useful VS Code extensions. |
| `microrender/.vscode/settings.json` | Stores project-level editor and Pico/CMake settings. |
| `microrender/.vscode/tasks.json` | Defines build or utility commands VS Code can run as tasks. |
| `microrender/.vscode/launch.json` | Defines launch/debug configurations. |
| `microrender/.vscode/c_cpp_properties.json` | Helps C/C++ IntelliSense find headers and compiler definitions. |
| `microrender/.vscode/cmake-kits.json` | Describes compiler/toolchain kits available to CMake Tools. |

## Shared Pico/RP2350 implementation

| File | Plain-English purpose |
|---|---|
| `shared/rp2350/mr_pico_demo.h` | Declares the Pico shared-game frontend entry point. |
| `shared/rp2350/mr_pico_demo.c` | Runs the shared game on Pico, including clock/LCD setup, input, rendering, FPS, and screenshots. |
| `shared/rp2350/mr_pico_stress_demo.h` | Declares the Pico stress frontend entry point. |
| `shared/rp2350/mr_pico_stress_demo.c` | Runs stress scenes and all Pico-specific presentation experiments. |
| `shared/rp2350/mr_pico_ili9341.h` | Declares the LCD driver state and operations. |
| `shared/rp2350/mr_pico_ili9341.c` | Initializes the ILI9341, configures SPI/DMA, selects windows, and transmits RGB565 pixels. |
| `shared/rp2350/mr_pico_screenshot.h` | Declares the app-independent screenshot service. |
| `shared/rp2350/mr_pico_screenshot.c` | Parses screenshot commands and streams a clean tiled RGB565 frame over USB. |

## Portable renderer and shared game

| File | Plain-English purpose |
|---|---|
| `shared/src/gfx_config.h` | Isolates compiler, pointer, integer-width, and renderer-limit differences. |
| `shared/src/gfx_color.h` | Defines logical RGB565 pixels and optional legacy INDEX8 compatibility. |
| `shared/src/gfx_fixed.h` | Defines integer fixed-point math used where fractions are needed without floats. |
| `shared/src/gfx.h` | Public renderer structures, flags, callbacks, and function declarations. |
| `shared/src/gfx.c` | Core renderer: tiles, clips, primitives, sprites, RLE, tilemaps, dirty regions, and frame traversal. |
| `shared/src/gfx_triangle.c` | Filled-triangle scanline rasterizer. |
| `shared/src/gfx_rgb444.h` | Declares RGB565-to-packed-12-bit conversion. |
| `shared/src/gfx_rgb444.c` | Converts RGB565 to 12 bpp for panels with a 12-bit interface. Currently unused: measured on SPI it lost more to byte-framing overhead than it saved in bytes. |
| `shared/src/gfx_font5x7.h` | Declares tiny-font drawing functions. |
| `shared/src/gfx_font5x7.c` | Stores and draws the built-in 5×7 bitmap font. |
| `shared/src/gfx_engine.h` | Declares actors, animations, collision maps, cameras, and helpers. |
| `shared/src/gfx_engine.c` | Implements actor movement, collision resolution, animation, cameras, and dirty tracking. |
| `shared/src/gfx_pack.h` | Declares the compact `.MRP` package reader and entry types. |
| `shared/src/gfx_pack.c` | Opens, validates, searches, and reads package entries, primarily for DOS runtime loading. |
| `shared/src/mr_demo_input.h` | Defines the common action/input structure used by every frontend. |
| `shared/src/mr_autodemo.h` | Declares deterministic automatic-demo input. |
| `shared/src/mr_autodemo.c` | Generates repeatable movement and button actions without a human player. |
| `shared/src/mr_game_demo.h` | Defines all state and public operations for the shared playable demo. |
| `shared/src/mr_game_demo.c` | Implements map generation, actors, input, terrain, collision, enemies, pickups, particles, camera, HUD, and rendering. |
| `shared/src/mr_stress_test.h` | Defines stress configuration, actors, metrics, buffers, and limits. |
| `shared/src/mr_stress_test.c` | Implements the moving-sprite performance scene and metrics. |
| `shared/src/mr_strbuf.h` | Declares bounded low-level text-formatting helpers. |
| `shared/src/mr_strbuf.c` | Implements portable number/string formatting without unsafe `sprintf` use. |

## Source assets

| File | Plain-English purpose |
|---|---|
| `shared/assets/project.json` | Master asset manifest telling the packer what to build. |
| `shared/assets/maps/demo_map.json` | Source map data for the asset-pipeline demo. |
| `shared/assets/sprites/tiles.bmp` | Source tileset artwork. |
| `shared/assets/sprites/player_sheet.bmp` | Source player animation sheet. |
| `shared/assets/sprites/player_sheet.json` | Metadata describing frames or animation regions in the player sheet. |
| `shared/assets/sprites/pickup.bmp` | Source pickup artwork. |
| `shared/assets/audio/pickup.wav` | Source pickup sound. |

## Asset tools and examples

| File | Plain-English purpose |
|---|---|
| `shared/tools/mr_pack.py` | Modern Python packer that converts source assets into `GAME.MRP`. |
| `shared/tools/mr_embed.py` | Converts the package bytes into C source for firmware embedding. |
| `shared/tools/mr_asset.c` | Native conversion/asset utility used for examples and alternate workflows. |
| `shared/tools/build_mr_asset.bat` | Builds the native asset utility on Windows. |
| `shared/tools/examples/convert_examples.bat` | Runs example source conversions. |
| `shared/tools/examples/pack_modern_examples.bat` | Demonstrates the modern package-building workflow. |
| `shared/tools/examples/aseprite_demo.json` | Example Aseprite-style metadata input. |
| `shared/tools/examples/tiled_demo.json` | Example Tiled-style map input. |
| `shared/tools/examples/checker8.bmp` | Example indexed bitmap asset. |
| `shared/tools/examples/masked8.bmp` | Example bitmap with transparent/masked regions. |
| `shared/tools/examples/demo.mrp` | Example compiled package. |
| `shared/tools/examples/demo_mrp.h` | Example package data represented as a C header. |

## DOS project

| File | Plain-English purpose |
|---|---|
| `microrender_dos/build_watcom.bat` | Compiles and links tiled/raw shared-game DOS executables with Open Watcom. |
| `microrender_dos/build_watcom_stress.bat` | Compiles and links tiled/raw DOS stress executables. |
| `microrender_dos/start_watcom_here.bat` | Opens or configures a shell with the Watcom environment ready. |
| `microrender_dos/show_bench_csv.bat` | Helps display or process DOS benchmark CSV output. |
| `microrender_dos/dos/dos_main.c` | Minimal DOS game executable entry point. |
| `microrender_dos/dos/dos_app.h` | Declares the DOS game application entry point. |
| `microrender_dos/dos/dos_app.c` | DOS game loop, options, timing, input translation, renderer setup, and cleanup. |
| `microrender_dos/dos/dos_keyboard.h` | Declares scan-code state and interrupt-handler operations. |
| `microrender_dos/dos/dos_keyboard.c` | Installs/removes interrupt 9 and records held-key state. |
| `microrender_dos/dos/dos_vga.h` | Declares VGA Mode X and presentation operations. |
| `microrender_dos/dos/dos_vga.c` | Programs VGA, converts RGB565 to palette indices, writes planes, and handles vblank/timing. |
| `microrender_dos/dos/dos_stress_app.c` | DOS loop and command-line wrapper for the shared stress test. |
| `microrender_dos/tools/record_dosbox_window_ffmpeg.ps1` | Captures a DOSBox window using FFmpeg for demonstrations. |

## Raylib desktop project

| File | Plain-English purpose |
|---|---|
| `microrender_raylib/CMakeLists.txt` | Builds the desktop frontend and links the selected/pinned Raylib. |
| `microrender_raylib/main.c` | Desktop window, input translation, shared game/stress loops, texture uploads, and presentation modes. |

## Command implementation

| File | Plain-English purpose |
|---|---|
| `scripts/mr_build.bat` | Implements all build targets and friendly options. |
| `scripts/mr_run.bat` | Runs tests, benchmarks, Raylib, and DOSBox programs. |
| `scripts/mr_clean.bat` | Deletes build outputs while preserving source. |
| `scripts/mr_tools.bat` | Locates required external tools. |
| `scripts/mr_preset_flags.py` | Helps derive or normalize Pico preset/build settings. |
| `scripts/mr_frmctr_sweep.bat` | Builds one Pico image per ILI9341 panel-refresh setting, for finding the fastest value a given panel tolerates. |

## Tests

| File | Plain-English purpose |
|---|---|
| `tests/CMakeLists.txt` | Builds the portable test library and test executables. |
| `tests/CMakePresets.json` | Named debug, sanitizer, release, and compatibility test recipes. |
| `tests/README.md` | Explains what the tests cover and how to run them. |
| `tests/mr_test_support.h` | Common fake framebuffer, flush, assertion, and test helpers. |
| `tests/mr_test_unit.c` | Renderer and utility correctness tests. |
| `tests/mr_test_game.c` | Shared-game behavior and regression tests. |
| `tests/mr_test_fuzz.c` | Deterministic hostile-coordinate and malformed-data testing. |
| `tests/mr_test_bench.c` | Comparative host performance measurements. |
| `tests/raylib_stub/CMakeLists.txt` | Builds the fake Raylib library used by CI. |
| `tests/raylib_stub/README.md` | Explains the scope and limitations of the stub. |
| `tests/raylib_stub/raylib.h` | Minimal subset of Raylib declarations needed by the frontend. |
| `tests/raylib_stub/raylib_stub.c` | No-window implementations that let the frontend execute headlessly. |

---

# Appendix B: which files to read, in order

Reading alphabetically is not the easiest way to understand the project. A beginner should use this order:

1. `shared/src/mr_demo_input.h` — see the tiny common input language.
2. `shared/src/mr_game_demo.h` — see what game state exists.
3. `shared/src/mr_game_demo.c` — see update and render separated.
4. `shared/src/gfx.h` — learn the renderer’s public data and callbacks.
5. `shared/src/gfx.c` — follow tiled rendering and one sprite path.
6. `shared/src/gfx_engine.h/.c` — understand actors, camera, and collision.
7. `microrender_raylib/main.c` — see the simplest visible platform adapter.
8. `shared/rp2350/mr_pico_demo.c` — compare the same shared game on hardware.
9. `shared/rp2350/mr_pico_ili9341.c` — inspect the SPI/DMA display boundary.
10. `microrender_dos/dos/dos_app.c` — compare the DOS adapter.
11. `microrender_dos/dos/dos_vga.c` — inspect the VGA boundary.
12. `shared/src/mr_stress_test.c` — understand controlled performance work.
13. `shared/rp2350/mr_pico_stress_demo.c` — compare presentation strategies.
14. `tests/mr_test_unit.c` and `mr_test_game.c` — see the intended behavior stated as checks.
15. `tests/mr_test_fuzz.c` — see how the edges are attacked.
16. `shared/tools/mr_pack.py` and `mr_embed.py` — follow assets from source to runtime.
17. `scripts/mr_build.bat` and CMake files — study the toolchains last, after knowing what they build.
