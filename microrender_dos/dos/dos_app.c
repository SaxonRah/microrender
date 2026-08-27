#include "dos_app.h"

/*
    MicroRender DOS frontend, rewritten from scratch.

    This file intentionally owns only DOS-specific work:
      - 320x240 unchained VGA (Mode X) setup / restore
      - RGB565-to-fixed-RGB332 presentation conversion
      - keyboard input
      - optional scripted autoplay input
      - tile flush to A000
      - command-line wrapper

    The game/demo itself lives in shared/src/mr_game_demo.c.
    Pico and DOS now call the same shared demo core.
*/

#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#ifdef __WATCOMC__
#include <i86.h>
#endif

#include "gfx.h"
#include "gfx_engine.h"
#include "mr_autodemo.h"
#include "mr_demo_input.h"
#include "mr_game_demo.h"
#include "dos_keyboard.h"
#include "dos_vga.h"
#include "mr_timestep.h"

#ifndef __WATCOMC__
#error This DOS frontend expects Open Watcom for the 16-bit DOS target.
#endif

#if GFX_COLOR_FORMAT != GFX_COLOR_FORMAT_RGB565
#error DOS frontend expects the shared RGB565 renderer.
#endif

#ifndef MK_FP
#define MK_FP(seg, ofs)                                                        \
  ((void __far *)((((unsigned long)(seg)) << 16) | ((unsigned long)(ofs))))
#endif

#define DOS_SCREEN_W 320
#define DOS_SCREEN_H 240
#ifndef MR_DOS_TILE_H
#define MR_DOS_TILE_H 16
#endif
#define DOS_TILE_H MR_DOS_TILE_H
#ifndef MR_DOS_VSYNC
#define MR_DOS_VSYNC 0
#endif
#ifndef MR_DOS_PRESENT_MODE
#define MR_DOS_PRESENT_MODE 1 /* 0 raw full-frame staging, 1 direct tiled */
#endif
#define DOS_FRAME_PIXELS ((long)DOS_SCREEN_W * (long)DOS_SCREEN_H)

typedef struct dos_options {
  int autoplay;
  int frames_limit;
  int wait_key_on_exit;
  int had_unknown_option;
  int show_help;
  int vsync;
  const char *shot_path;
  const char *report_path;
} dos_options_t;

static mr_timestep_t dos_step;
static unsigned long dos_sim_ticks;
static uint16_t dos_raw_prev_buttons;

static gfx_color_t dos_tile_buffer[DOS_SCREEN_W * DOS_TILE_H];
static mr_game_demo_t dos_game;
static gfx_renderer_t dos_renderer;
#if MR_DOS_PRESENT_MODE == 0
static gfx_color_t __huge *dos_raw_frame;
#endif

/* Screenshot capture.
 *
 * Deliberately matches the Pico's MRSHOT1 serial format, so one parser reads
 * captures from every platform and a DOS file is byte-comparable with one
 * pulled off hardware:
 *
 *     MRSHOT1 <width> <height> <bytes>\n
 *     <width*height little-endian RGB565 pixels>
 *
 * The frame is streamed a tile at a time through the renderer's own flush
 * hook rather than from a full-frame buffer. The tiled present mode never
 * holds a complete frame, and a 320x240 RGB565 frame is 150 KiB -- more than
 * a real-mode program should be allocating just to save a picture. Swapping
 * the flush callback re-renders the scene into the existing tile buffer and
 * writes each strip out as it is produced.
 *
 * Rows arrive in order and each is a contiguous run of w pixels, so no
 * seeking is needed; x is always 0 and w always the full width for this
 * renderer's tiling.
 */
/* 0 not requested, 1 written, -1 failed. Reported after the video
   mode is restored: anything printed while Mode X is active goes to
   pixels rather than to the screen. */
/* True when a filename fits DOS 8.3: up to eight characters, optionally a
   dot and up to three more. Anything longer is silently truncated on write. */
static int dos_name_is_8_3(const char *name) {
  int stem = 0;
  int ext = 0;
  int seen_dot = 0;

  if (!name)
    return 0;

  while (*name) {
    if (*name == '.') {
      if (seen_dot)
        return 0;
      seen_dot = 1;
    } else if (seen_dot) {
      ++ext;
    } else {
      ++stem;
    }
    ++name;
  }
  return stem >= 1 && stem <= 8 && ext <= 3;
}

static int dos_capture_status = 0;
static FILE *dos_shot_file = 0;
static int dos_shot_failed = 0;

static void dos_shot_flush(gfx_renderer_t GFX_PTR *r, int x, int y, int w,
                           int h, const gfx_color_t GFX_PTR *pixels,
                           void GFX_PTR *user) {
  int row;

  (void)r;
  (void)x;
  (void)y;
  (void)user;

  if (!dos_shot_file || dos_shot_failed || !pixels || w <= 0 || h <= 0)
    return;

  for (row = 0; row < h; ++row) {
    size_t got = fwrite(pixels + (long)row * (long)w, sizeof(gfx_color_t),
                        (size_t)w, dos_shot_file);
    if (got != (size_t)w) {
      dos_shot_failed = 1;
      return;
    }
  }
}

static int dos_write_shot(const char *path, gfx_renderer_t GFX_PTR *r,
                          void (*draw_scene)(gfx_renderer_t GFX_PTR *r,
                                             void GFX_PTR *scene_user),
                          void GFX_PTR *scene_user) {
  gfx_flush_fn saved_flush;
  void GFX_PTR *saved_user;
  int ok;

  if (!path || !r)
    return 0;

  dos_shot_file = fopen(path, "wb");
  if (!dos_shot_file)
    return 0;
  dos_shot_failed = 0;

  fprintf(dos_shot_file, "MRSHOT1 %d %d %lu\n", DOS_SCREEN_W, DOS_SCREEN_H,
          (unsigned long)DOS_FRAME_PIXELS * (unsigned long)sizeof(gfx_color_t));

  saved_flush = r->flush;
  saved_user = r->user;
  r->flush = dos_shot_flush;
  r->user = 0;

  gfx_render_tiled(r, draw_scene, scene_user, GFX_RGB565_BLACK);

  r->flush = saved_flush;
  r->user = saved_user;

  ok = !dos_shot_failed;
  if (fclose(dos_shot_file) != 0)
    ok = 0;
  dos_shot_file = 0;
  return ok;
}

/* Machine-readable run summary, one key=value per line, matching what the
   Raylib frontend writes so the capture harness needs no special case. */
static int dos_write_report(const char *path, const dos_options_t *opt,
                            unsigned long frames, unsigned long sim_ticks,
                            unsigned long elapsed_us) {
  FILE *f;
  double secs;

  if (!path)
    return 0;
  f = fopen(path, "wb");
  if (!f)
    return 0;

  secs = (double)elapsed_us / 1000000.0;
  fprintf(f, "platform=dos\n");
  fprintf(f, "demo=game\n");
  fprintf(f, "width=%d\nheight=%d\n", DOS_SCREEN_W, DOS_SCREEN_H);
  fprintf(f, "tile_h=%d\n", DOS_TILE_H);
  fprintf(f, "present_mode=%d\n", MR_DOS_PRESENT_MODE);
  fprintf(f, "vsync=%d\n", opt ? opt->vsync : 0);
  fprintf(f, "frames=%lu\n", frames);
  fprintf(f, "elapsed_s=%.4f\n", secs);
  fprintf(f, "fps_avg=%.2f\n", secs > 0.0 ? (double)frames / secs : 0.0);
  fprintf(f, "sim_ticks=%lu\n", sim_ticks);
  fprintf(f, "sim_hz=%.2f\n", secs > 0.0 ? (double)sim_ticks / secs : 0.0);
  fclose(f);
  return 1;
}

static int arg_eq(const char *a, const char *b) {
  while (*a && *b) {
    char ca;
    char cb;
    ca = *a;
    cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb + ('a' - 'A'));
    if (ca != cb)
      return 0;
    ++a;
    ++b;
  }
  return *a == 0 && *b == 0;
}

static int arg_starts_with(const char *a, const char *prefix) {
  while (*prefix) {
    char ca;
    char cb;
    ca = *a;
    cb = *prefix;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb + ('a' - 'A'));
    if (ca != cb)
      return 0;
    ++a;
    ++prefix;
  }
  return 1;
}

static void dos_enter_video(void) {
  dos_vga_enter();
  dos_keyboard_install();
}

static void dos_leave_video(void) {
  /* Restore the interrupt vector before the video mode, and unconditionally:
     leaving a dangling INT 9 pointing into freed memory hangs DOS. Both calls
     are idempotent, so this is safe from atexit(). */
  dos_keyboard_remove();
  dos_vga_leave();
}


#if MR_DOS_PRESENT_MODE == 0
static void dos_capture_raw_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w,
                                 int h,
                                 const gfx_color_t GFX_PTR *pixels,
                                 void GFX_PTR *user) {
  int row;
  int col;
  int stride;
  (void)user;
  if (!dos_raw_frame || !pixels || w <= 0 || h <= 0)
    return;
  stride = r ? r->tile_stride : w;
  for (row = 0; row < h; ++row) {
    gfx_color_t __huge *dst;
    const gfx_color_t GFX_PTR *src;
    dst = dos_raw_frame + (long)(y + row) * DOS_SCREEN_W + x;
    src = pixels + (long)row * stride;
    for (col = 0; col < w; ++col)
      dst[col] = src[col];
  }
}
#endif

static void dos_draw_game_scene(gfx_renderer_t GFX_PTR *r, void GFX_PTR *user) {
  mr_game_demo_t *game;
  game = (mr_game_demo_t *)user;
  mr_game_demo_render(game, r);
}

/* Held-key input via the INT 9 handler in dos_keyboard.c.

   The BIOS keystroke queue reports key events subject to the typematic repeat
   delay, so polling it makes the player stall for about half a second after
   each direction press. Reading physical key state instead makes movement
   respond on the frame the key goes down. dos_read_keyboard_input_bios() below
   is kept as a fallback for when the handler could not be installed. */
static void dos_read_keyboard_input_raw(mr_demo_input_t *input) {
  if (!input)
    return;

  input->dx = 0;
  input->dy = 0;
  input->buttons = 0;

  if (dos_key_down(DOS_SC_UP) || dos_key_down(DOS_SC_W))
    input->buttons |= MR_DEMO_INPUT_UP;
  if (dos_key_down(DOS_SC_DOWN) || dos_key_down(DOS_SC_S))
    input->buttons |= MR_DEMO_INPUT_DOWN;
  if (dos_key_down(DOS_SC_LEFT) || dos_key_down(DOS_SC_A))
    input->buttons |= MR_DEMO_INPUT_LEFT;
  if (dos_key_down(DOS_SC_RIGHT) || dos_key_down(DOS_SC_D))
    input->buttons |= MR_DEMO_INPUT_RIGHT;
  if (dos_key_down(DOS_SC_SPACE))
    input->buttons |= MR_DEMO_INPUT_ACTION;
  if (dos_key_down(DOS_SC_ENTER))
    input->buttons |= MR_DEMO_INPUT_START;
  if (dos_key_down(DOS_SC_P))
    input->buttons |= MR_DEMO_INPUT_PAUSE;
  if (dos_key_down(DOS_SC_GRAVE))
    input->buttons |= MR_DEMO_INPUT_DEBUG;
  if (dos_key_down(DOS_SC_ESC) || dos_key_down(DOS_SC_Q))
    input->buttons |= MR_DEMO_INPUT_QUIT;

  /* INT 9 gives us held physical state. Convert action/toggle keys to press
     edges here so holding P does not toggle pause every simulation tick. */
  {
    uint16_t raw_buttons;
    raw_buttons = input->buttons;
    input->buttons =
        (uint16_t)((raw_buttons & ~MR_DEMO_INPUT_EDGE_MASK) |
                   ((raw_buttons & MR_DEMO_INPUT_EDGE_MASK) &
                    ~(dos_raw_prev_buttons & MR_DEMO_INPUT_EDGE_MASK)));
    dos_raw_prev_buttons = raw_buttons;
  }

  if (input->buttons & MR_DEMO_INPUT_LEFT)
    input->dx -= 1;
  if (input->buttons & MR_DEMO_INPUT_RIGHT)
    input->dx += 1;
  if (input->buttons & MR_DEMO_INPUT_UP)
    input->dy -= 1;
  if (input->buttons & MR_DEMO_INPUT_DOWN)
    input->dy += 1;
}

static void dos_read_keyboard_input_bios(mr_demo_input_t *input) {
  int ch;

  if (!input)
    return;

  input->dx = 0;
  input->dy = 0;
  input->buttons = 0;

  while (kbhit()) {
    ch = getch();

    if (ch == 0 || ch == 0xE0) {
      ch = getch();
      switch (ch) {
      case 72:
        input->buttons |= MR_DEMO_INPUT_UP;
        break;
      case 80:
        input->buttons |= MR_DEMO_INPUT_DOWN;
        break;
      case 75:
        input->buttons |= MR_DEMO_INPUT_LEFT;
        break;
      case 77:
        input->buttons |= MR_DEMO_INPUT_RIGHT;
        break;
      default:
        break;
      }
    } else {
      switch (ch) {
      case 27:
      case 'q':
      case 'Q':
        input->buttons |= MR_DEMO_INPUT_QUIT;
        break;

      case 'w':
      case 'W':
      case '8':
        input->buttons |= MR_DEMO_INPUT_UP;
        break;

      case 's':
      case 'S':
      case '2':
        input->buttons |= MR_DEMO_INPUT_DOWN;
        break;

      case 'a':
      case 'A':
      case '4':
        input->buttons |= MR_DEMO_INPUT_LEFT;
        break;

      case 'd':
      case 'D':
      case '6':
        input->buttons |= MR_DEMO_INPUT_RIGHT;
        break;

      case ' ':
        input->buttons |= MR_DEMO_INPUT_ACTION;
        break;

      case '\r':
        input->buttons |= MR_DEMO_INPUT_START;
        break;

      case 'p':
      case 'P':
        input->buttons |= MR_DEMO_INPUT_PAUSE;
        break;

      case '`':
      case '~':
        input->buttons |= MR_DEMO_INPUT_DEBUG;
        break;

      default:
        break;
      }
    }
  }

  if (input->buttons & MR_DEMO_INPUT_LEFT)
    input->dx -= 1;
  if (input->buttons & MR_DEMO_INPUT_RIGHT)
    input->dx += 1;
  if (input->buttons & MR_DEMO_INPUT_UP)
    input->dy -= 1;
  if (input->buttons & MR_DEMO_INPUT_DOWN)
    input->dy += 1;
}

static void dos_read_keyboard_input(mr_demo_input_t *input) {
  if (dos_keyboard_installed())
    dos_read_keyboard_input_raw(input);
  else
    dos_read_keyboard_input_bios(input);
}

static void dos_print_usage(void) {
  printf("MicroRender DOS shared-game frontend\n");
  printf("\n");
  printf("Usage:\n");
  printf("  mrender.exe [options]\n");
  printf("\n");
  printf("Options:\n");
  printf("  /auto, /autorun       feed shared autodemo input instead of "
         "keyboard\n");
  printf("  /frames N             exit after N frames, useful for capture "
         "scripts\n");
  printf("  /wait                 wait for a key after exiting video mode\n");
  printf("  /vsync, /novsync      enable/disable VGA retrace wait\n");
  printf("  /?, /help             show this help\n");
  printf("\n");
  printf("Keyboard:\n");
  printf("  arrows/WASD move, Enter/Space start/action, P pause, ` debug, "
         "Esc/Q quit\n");
}

static void dos_parse_options(int argc, char **argv, dos_options_t *opt) {
  int i;

  opt->autoplay = 0;
  opt->shot_path = 0;
  opt->report_path = 0;
  opt->frames_limit = 0;
  opt->wait_key_on_exit = 0;
  opt->show_help = 0;
  opt->had_unknown_option = 0;
  opt->vsync = MR_DOS_VSYNC ? 1 : 0;

  for (i = 1; i < argc; ++i) {
    if (arg_eq(argv[i], "/?") || arg_eq(argv[i], "-?") ||
        arg_eq(argv[i], "/help") || arg_eq(argv[i], "--help")) {
      opt->show_help = 1;
    } else if (arg_eq(argv[i], "/auto") || arg_eq(argv[i], "-auto") ||
               arg_eq(argv[i], "/autorun") || arg_eq(argv[i], "-autorun")) {
      opt->autoplay = 1;
    } else if ((arg_eq(argv[i], "/shot") || arg_eq(argv[i], "-shot")) &&
               i + 1 < argc) {
      ++i;
      opt->shot_path = argv[i];
    } else if (arg_starts_with(argv[i], "/shot=") ||
               arg_starts_with(argv[i], "-shot=")) {
      opt->shot_path = argv[i] + 6;
    } else if ((arg_eq(argv[i], "/report") || arg_eq(argv[i], "-report")) &&
               i + 1 < argc) {
      ++i;
      opt->report_path = argv[i];
    } else if (arg_starts_with(argv[i], "/report=") ||
               arg_starts_with(argv[i], "-report=")) {
      opt->report_path = argv[i] + 8;
    } else if (arg_eq(argv[i], "/wait") || arg_eq(argv[i], "-wait")) {
      opt->wait_key_on_exit = 1;
    } else if (arg_eq(argv[i], "/vsync") || arg_eq(argv[i], "-vsync")) {
      opt->vsync = 1;
    } else if (arg_eq(argv[i], "/novsync") || arg_eq(argv[i], "-novsync")) {
      opt->vsync = 0;
    } else if ((arg_eq(argv[i], "/frames") || arg_eq(argv[i], "-frames")) &&
               i + 1 < argc) {
      ++i;
      opt->frames_limit = atoi(argv[i]);
    } else if (arg_starts_with(argv[i], "/frames=") ||
               arg_starts_with(argv[i], "-frames=")) {
      opt->frames_limit = atoi(argv[i] + 8);
    } else if (argv[i][0] == '/' || argv[i][0] == '-') {
      /* Say so rather than ignoring it. Several runner scripts and the old
         usage text advertised flags this frontend never parsed (/fast, /modex,
         /hwscroll, /tilemap, /bench, /demo), so a silent no-op looked like a
         broken feature instead of a stale argument. */
      printf("warning: unrecognised option \"%s\" ignored\n", argv[i]);
      opt->had_unknown_option = 1;
    }
  }

  if (opt->had_unknown_option) {
    printf("Run with /? for the supported options.\n");
  }
}

static int dos_run_shared_game(const dos_options_t *opt) {
  mr_demo_input_t input;
  unsigned long frame;
  unsigned long start_us;
  unsigned long start_tick;
  unsigned long fps_tick;
  unsigned long fps_frame;
  int running;
  uint16_t pending_edges;

#if MR_DOS_PRESENT_MODE == 0
  /* dos_app_main allocates this before entering graphics mode so allocation
     failures remain visible in the DOS text console. */
  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, dos_capture_raw_tile, 0);
#else
  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, dos_vga_flush_tile, 0);
#endif

  mr_game_demo_init(&dos_game, DOS_SCREEN_W, DOS_SCREEN_H);
  printf("DOS Mode X: %dx%d logical RGB565, RGB332 VGA output, mode=%s, tile_h=%d, vsync=%s\n",
         DOS_SCREEN_W, DOS_SCREEN_H,
         MR_DOS_PRESENT_MODE == 0 ? "raw" : "tiled", DOS_TILE_H,
         opt->vsync ? "on" : "off");
  mr_autodemo_reset();

  frame = 0UL;
  dos_sim_ticks = 0UL;
  dos_raw_prev_buttons = 0u;
  pending_edges = 0u;
  mr_timestep_init(&dos_step, MR_GAME_TICK_HZ, 5);
  start_us = dos_vga_micros();
  start_tick = dos_vga_ticks();
  fps_tick = start_tick;
  fps_frame = 0UL;
  mr_game_demo_set_fps10(&dos_game, 0ul, 0ul);
  running = 1;

  while (running) {
    if (!opt->autoplay) {
      dos_read_keyboard_input(&input);
      pending_edges |= input.buttons & MR_DEMO_INPUT_EDGE_MASK;
      input.buttons =
          (uint16_t)((input.buttons & ~MR_DEMO_INPUT_EDGE_MASK) |
                     pending_edges);
    }

    {
      /* Scripted input is indexed by simulation tick rather than frame, so the
         autopilot follows the same path whether the host manages 15 frames a
         second or 140. Live held input is sampled once per frame. One-shot
         buttons stay pending across zero-step render frames, and are consumed
         by only the first catch-up step. */
      int steps = mr_timestep_advance(&dos_step, dos_vga_micros());
      while (steps-- > 0) {
        if (opt->autoplay)
          mr_autodemo_input(dos_sim_ticks, &input);
        mr_game_demo_tick(&dos_game, &input);
        ++dos_sim_ticks;
        if (!opt->autoplay) {
          pending_edges = 0u;
          input.buttons =
              (uint16_t)(input.buttons & ~MR_DEMO_INPUT_EDGE_MASK);
        }
      }
    }
    gfx_render_tiled(&dos_renderer, dos_draw_game_scene, &dos_game,
                     GFX_RGB565_BLACK);
#if MR_DOS_PRESENT_MODE == 0
    /* Deliberately unoptimized baseline: only after all strips have been
       rasterized into one logical frame do we upload the complete frame. */
    dos_vga_present_rgb565_frame(dos_raw_frame);
#endif

    ++frame;

    {
      unsigned long now_tick;
      unsigned long dt;
      now_tick = dos_vga_ticks();
      dt = now_tick - fps_tick;
      if (dt >= 9ul) {
        unsigned long df;
        unsigned long total_dt;
        unsigned long fps10;
        unsigned long avg_fps10;
        df = frame - fps_frame;
        fps10 = (df * 182ul) / dt;
        total_dt = now_tick - start_tick;
        avg_fps10 = total_dt ? (frame * 182ul) / total_dt : 0ul;
        mr_game_demo_set_fps10(&dos_game, fps10, avg_fps10);
        fps_tick = now_tick;
        fps_frame = frame;
      }
    }

    if (mr_game_demo_quit_requested(&dos_game)) {
      running = 0;
    }

    if (opt->frames_limit > 0 && frame >= (unsigned long)opt->frames_limit) {
      running = 0;
    }

    if (opt->vsync)
      dos_vga_wait_vblank();
  }

  /* Capture after the loop, while the renderer and scene are still valid but
     nothing further will disturb them. The screenshot re-renders the current
     state into the tile buffer, so it reflects the last simulated frame
     regardless of which present mode was in use. */
  if (opt->shot_path)
    dos_capture_status =
        dos_write_shot(opt->shot_path, &dos_renderer, dos_draw_game_scene,
                       &dos_game)
            ? 1
            : -1;
  if (opt->report_path) {
    unsigned long end_us = dos_vga_micros();
    (void)dos_write_report(opt->report_path, opt, frame, dos_sim_ticks,
                           end_us - start_us);
  }

  return 0;
}

int dos_app_main(int argc, char **argv) {
  dos_options_t opt;
  int rc;

  dos_parse_options(argc, argv, &opt);

  if (opt.show_help) {
    dos_print_usage();
    return 0;
  }

  rc = 0;

#if MR_DOS_PRESENT_MODE == 0
  dos_raw_frame =
      (gfx_color_t __huge *)halloc(DOS_FRAME_PIXELS, sizeof(gfx_color_t));
  if (!dos_raw_frame) {
    printf("ERROR: raw DOS mode needs a 150 KiB huge-memory framebuffer.\n");
    return 1;
  }
#endif

  /* Safety net: dos_leave_video() restores both the INT 9 vector and the text
     mode, and is idempotent. Registering it means an exit() from anywhere
     below cannot leave DOS with a dangling interrupt vector. */
  atexit(dos_leave_video);

  dos_enter_video();

  rc = dos_run_shared_game(&opt);

  dos_leave_video();

  /* Now that text mode is back, say whether the capture worked. Silence here
     is what made a failed fopen look like the whole run had done nothing. */
  if (dos_capture_status > 0) {
    /* Say what DOS actually created, not what was asked for. A name like
       dos.shot is silently truncated to DOS.SHO by the 8.3 filesystem, and a
       script waiting on the requested name waits forever while the capture
       sits on disk under a different one. */
    printf("capture: wrote %s", opt.shot_path);
    if (!dos_name_is_8_3(opt.shot_path))
      printf("  (NOTE: 8.3 truncation - check the actual filename)");
    printf("\n");
  } else if (dos_capture_status < 0) {
    printf("capture: FAILED to write \"%s\" - check the filename reached the\n"
           "         program unquoted, and that the drive is writable.\n",
           opt.shot_path ? opt.shot_path : "(null)");
  }

#if MR_DOS_PRESENT_MODE == 0
  hfree(dos_raw_frame);
  dos_raw_frame = 0;
#endif

  if (opt.wait_key_on_exit) {
    printf("Press any key...\n");
    getch();
  }

  return rc;
}
