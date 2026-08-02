#define GFX_COLOR_INDEX8 1

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "mr_stress_test.h"
#include "dos_vga.h"

#define DOS_SCREEN_W 320
#define DOS_SCREEN_H 200
#define DOS_TILE_H 16

static gfx_renderer_t dos_renderer;
static gfx_color_t dos_tile_buffer[DOS_SCREEN_W * DOS_TILE_H];
static mr_stress_test_t stress;



static void draw_stress_scene(gfx_renderer_t GFX_PTR *r, void GFX_PTR *user) {
  mr_stress_render(r, (mr_stress_test_t GFX_PTR *)user);
}

static int parse_int_arg(int argc, char **argv, int *i, int fallback) {
  if (*i + 1 >= argc)
    return fallback;
  ++(*i);
  return atoi(argv[*i]);
}

static void print_usage(void) {
  printf("MicroRender RLE/collision stress test\n");
  printf("Usage: mstress [/sprites N] [/frames N] [/novsync] [/noflush] "
         "[/nohud]\n");
  printf("               [/notri] [/nostats] [/fullstats] [/statsrate N]\n");
  printf("               [/notile] [/nocollide] [/target N]\n");
  printf("Examples:\n");
  printf("  mstress /sprites 512 /frames 2100 /novsync\n");
  printf("  mstress /sprites 1024 /frames 2100 /novsync\n");
  printf("  mstress /sprites 1024 /frames 2100 /novsync /noflush\n");
  printf("  mstress /sprites 1024 /frames 2100 /novsync /fullstats\n");
}

int main(int argc, char **argv) {
  mr_stress_config_t cfg;
  mr_stress_metrics_t m;
  unsigned long start_tick;
  unsigned long end_tick;
  unsigned long fps_tick;
  unsigned long fps_frame;
  unsigned long fps10;
  unsigned long avg_fps10;
  unsigned long frames;
  int frame_limit;
  int no_vsync;
  int i;

  frame_limit = 2100;
  no_vsync = 0;

  mr_stress_config_defaults(&cfg, DOS_SCREEN_W, DOS_SCREEN_H);
  cfg.sprite_count = 512;
  cfg.target_fps = 120;
  cfg.stats_sample_rate = 8;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "/?") == 0 || strcmp(argv[i], "-?") == 0 ||
        strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    } else if (strcmp(argv[i], "/sprites") == 0 ||
               strcmp(argv[i], "-sprites") == 0) {
      cfg.sprite_count = parse_int_arg(argc, argv, &i, cfg.sprite_count);
    } else if (strcmp(argv[i], "/frames") == 0 ||
               strcmp(argv[i], "-frames") == 0) {
      frame_limit = parse_int_arg(argc, argv, &i, frame_limit);
    } else if (strcmp(argv[i], "/target") == 0 ||
               strcmp(argv[i], "-target") == 0) {
      cfg.target_fps = parse_int_arg(argc, argv, &i, cfg.target_fps);
    } else if (strcmp(argv[i], "/novsync") == 0 ||
               strcmp(argv[i], "-novsync") == 0) {
      no_vsync = 1;
    } else if (strcmp(argv[i], "/noflush") == 0 ||
               strcmp(argv[i], "-noflush") == 0 ||
               strcmp(argv[i], "/renderonly") == 0 ||
               strcmp(argv[i], "-renderonly") == 0) {
      dos_vga_set_flush_enabled(0);
    } else if (strcmp(argv[i], "/nohud") == 0 ||
               strcmp(argv[i], "-nohud") == 0) {
      cfg.features &= ~MR_STRESS_FEATURE_HUD;
    } else if (strcmp(argv[i], "/notri") == 0 ||
               strcmp(argv[i], "-notri") == 0) {
      cfg.features &= ~MR_STRESS_FEATURE_TRIANGLES;
    } else if (strcmp(argv[i], "/nostats") == 0 ||
               strcmp(argv[i], "-nostats") == 0) {
      cfg.features &= ~MR_STRESS_FEATURE_STATS;
    } else if (strcmp(argv[i], "/fullstats") == 0 ||
               strcmp(argv[i], "-fullstats") == 0) {
      cfg.stats_sample_rate = 1;
    } else if (strcmp(argv[i], "/statsrate") == 0 ||
               strcmp(argv[i], "-statsrate") == 0) {
      cfg.stats_sample_rate =
          parse_int_arg(argc, argv, &i, cfg.stats_sample_rate);
    } else if (strcmp(argv[i], "/notile") == 0 ||
               strcmp(argv[i], "-notile") == 0) {
      cfg.features &= ~MR_STRESS_FEATURE_TILEMAP;
    } else if (strcmp(argv[i], "/nocollide") == 0 ||
               strcmp(argv[i], "-nocollide") == 0) {
      cfg.features &= ~MR_STRESS_FEATURE_COLLISION;
    }
  }

  if (cfg.sprite_count < 1)
    cfg.sprite_count = 1;
  if (cfg.sprite_count > MR_STRESS_MAX_SPRITES)
    cfg.sprite_count = MR_STRESS_MAX_SPRITES;
  if (frame_limit <= 0)
    frame_limit = 2100;
  if (cfg.stats_sample_rate <= 0)
    cfg.stats_sample_rate = 8;

  printf("MicroRender stress: sprites=%d frames=%d vsync=%s flush=%s target=%d "
         "statsrate=%d\n",
         cfg.sprite_count, frame_limit, no_vsync ? "off" : "on",
         dos_vga_flush_enabled() ? "on" : "off", cfg.target_fps, cfg.stats_sample_rate);
  printf("Press ESC during the run to stop early.\n");

  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, dos_vga_flush_tile, 0);
  mr_stress_init(&stress, &cfg);

  dos_vga_enter();
  start_tick = dos_vga_ticks();
  fps_tick = start_tick;
  fps_frame = 0;
  fps10 = 0;
  avg_fps10 = 0;
  frames = 0;
  mr_stress_set_fps10(&stress, fps10, avg_fps10);

  if (!dos_vga_flush_enabled()) {
    int saved_flush;
    saved_flush = dos_vga_flush_enabled();
    dos_vga_set_flush_enabled(1);
    mr_stress_tick(&stress);
    gfx_render_tiled_no_clear(&dos_renderer, draw_stress_scene, &stress);
    delay(250);
    dos_vga_set_flush_enabled(saved_flush);
    start_tick = dos_vga_ticks();
    fps_tick = start_tick;
    fps_frame = 0;
    fps10 = 0;
    avg_fps10 = 0;
    frames = 0;
    mr_stress_set_fps10(&stress, fps10, avg_fps10);
  }

  while ((int)frames < frame_limit) {
    if (kbhit()) {
      int ch;
      ch = getch();
      if (ch == 27)
        break;
    }

    mr_stress_tick(&stress);
    gfx_render_tiled_no_clear(&dos_renderer, draw_stress_scene, &stress);
    ++frames;

    {
      unsigned long now_tick;
      unsigned long dt;

      now_tick = dos_vga_ticks();
      dt = now_tick - fps_tick;
      if (dt >= 9ul) {
        unsigned long df;
        unsigned long total_dt;

        df = frames - fps_frame;
        fps10 = (df * 182ul) / dt;
        total_dt = now_tick - start_tick;
        if (total_dt != 0ul)
          avg_fps10 = (frames * 182ul) / total_dt;
        mr_stress_set_fps10(&stress, fps10, avg_fps10);
        fps_tick = now_tick;
        fps_frame = frames;
      }
    }

    if (!no_vsync)
      dos_vga_wait_vblank();
  }

  end_tick = dos_vga_ticks();
  mr_stress_get_metrics(&stress, &m);

  dos_vga_leave();

  printf("MicroRender RLE/collision stress result\n");
  printf("sprites=%d frames=%lu ticks=%lu vsync=%s flush=%s\n",
         cfg.sprite_count, frames, end_tick - start_tick,
         no_vsync ? "off" : "on", dos_vga_flush_enabled() ? "on" : "off");
  if (end_tick != start_tick) {
    avg_fps10 = (frames * 182ul) / (end_tick - start_tick);
    printf("avg_fps ~= %lu.%lu  last_window=%lu.%lu\n", avg_fps10 / 10ul,
           avg_fps10 % 10ul, fps10 / 10ul, fps10 % 10ul);
  }
  printf("last frame: visible=%lu buckets=%lu drawn=%lu rejected=%lu runs=%lu "
         "pixels=%lu coll_checks=%lu coll_hits=%lu\n",
         m.sprites_visible, m.bucket_items, m.sprites_drawn, m.sprites_rejected,
         m.rle_runs_drawn, m.rle_pixels_copied, m.collision_checks,
         m.collision_hits);

  return 0;
}
