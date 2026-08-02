#include "dos_app.h"

/*
    MicroRender DOS frontend, rewritten from scratch.

    This file intentionally owns only DOS-specific work:
      - VGA mode 13h setup / restore
      - RGB332 palette setup
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

#ifndef __WATCOMC__
#error This DOS frontend expects Open Watcom for the 16-bit DOS target.
#endif

#if GFX_COLOR_FORMAT != GFX_COLOR_FORMAT_INDEX8
#error DOS frontend expects GFX_COLOR_INDEX8=1.
#endif

#ifndef MK_FP
#define MK_FP(seg, ofs)                                                        \
  ((void __far *)((((unsigned long)(seg)) << 16) | ((unsigned long)(ofs))))
#endif

#define DOS_SCREEN_W 320
#define DOS_SCREEN_H 200
#define DOS_TILE_H 16
#define DOS_TICK_HZ 70UL

typedef struct dos_options {
  int autoplay;
  int frames_limit;
  int wait_key_on_exit;
  int had_unknown_option;
  int show_help;
} dos_options_t;

static gfx_color_t dos_tile_buffer[DOS_SCREEN_W * DOS_TILE_H];
static mr_game_demo_t dos_game;
static gfx_renderer_t dos_renderer;

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
  printf("  /?, /help             show this help\n");
  printf("\n");
  printf("Keyboard:\n");
  printf("  arrows/WASD move, Enter/Space start/action, P pause, ` debug, "
         "Esc/Q quit\n");
}

static void dos_parse_options(int argc, char **argv, dos_options_t *opt) {
  int i;

  opt->autoplay = 0;
  opt->frames_limit = 0;
  opt->wait_key_on_exit = 0;
  opt->show_help = 0;
  opt->had_unknown_option = 0;

  for (i = 1; i < argc; ++i) {
    if (arg_eq(argv[i], "/?") || arg_eq(argv[i], "-?") ||
        arg_eq(argv[i], "/help") || arg_eq(argv[i], "--help")) {
      opt->show_help = 1;
    } else if (arg_eq(argv[i], "/auto") || arg_eq(argv[i], "-auto") ||
               arg_eq(argv[i], "/autorun") || arg_eq(argv[i], "-autorun")) {
      opt->autoplay = 1;
    } else if (arg_eq(argv[i], "/wait") || arg_eq(argv[i], "-wait")) {
      opt->wait_key_on_exit = 1;
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
  int running;

  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, dos_vga_flush_tile, 0);

  mr_game_demo_init(&dos_game, DOS_SCREEN_W, DOS_SCREEN_H);
  mr_autodemo_reset();

  frame = 0UL;
  running = 1;

  while (running) {
    if (opt->autoplay) {
      mr_autodemo_input(frame, &input);
    } else {
      dos_read_keyboard_input(&input);
    }

    mr_game_demo_tick(&dos_game, &input);
    gfx_render_tiled(&dos_renderer, dos_draw_game_scene, &dos_game,
                     GFX_RGB565_BLACK);

    ++frame;

    if (mr_game_demo_quit_requested(&dos_game)) {
      running = 0;
    }

    if (opt->frames_limit > 0 && frame >= (unsigned long)opt->frames_limit) {
      running = 0;
    }

    dos_vga_wait_vblank();
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

  /* Safety net: dos_leave_video() restores both the INT 9 vector and the text
     mode, and is idempotent. Registering it means an exit() from anywhere
     below cannot leave DOS with a dangling interrupt vector. */
  atexit(dos_leave_video);

  dos_enter_video();
  

  rc = dos_run_shared_game(&opt);

  dos_leave_video();

  if (opt.wait_key_on_exit) {
    printf("Press any key...\n");
    getch();
  }

  return rc;
}
