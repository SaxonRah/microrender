#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "gfx.h"
#include "mr_stress_test.h"
#include "dos_vga.h"

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


static gfx_renderer_t dos_renderer;
static gfx_color_t dos_tile_buffer[DOS_SCREEN_W * DOS_TILE_H];
static mr_stress_test_t stress;
#if MR_DOS_PRESENT_MODE == 0
static gfx_color_t __huge *dos_raw_frame;

static void capture_raw_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w,
                             int h, const gfx_color_t GFX_PTR *pixels,
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

static void draw_stress_scene(gfx_renderer_t GFX_PTR *r, void GFX_PTR *user) {
  mr_stress_render(r, (mr_stress_test_t GFX_PTR *)user);
}

/* DOS stress capture uses the same MRSHOT1 logical RGB565 format as the game,
   Raylib, and Pico. Re-render through a temporary flush callback after timing
   ends so capture work cannot affect the benchmark. */
static FILE *dos_shot_file;
static int dos_shot_failed;

static void dos_shot_flush(gfx_renderer_t GFX_PTR *r, int x, int y, int w,
                           int h, const gfx_color_t GFX_PTR *pixels,
                           void GFX_PTR *user) {
  int row;
  int stride;
  (void)x;
  (void)y;
  (void)user;
  if (!dos_shot_file || dos_shot_failed || !pixels || w <= 0 || h <= 0)
    return;
  stride = r ? r->tile_stride : w;
  for (row = 0; row < h; ++row) {
    size_t got;
    got = fwrite(pixels + (long)row * stride, sizeof(gfx_color_t),
                 (size_t)w, dos_shot_file);
    if (got != (size_t)w) {
      dos_shot_failed = 1;
      return;
    }
  }
}

static int dos_write_shot(const char *path) {
  gfx_flush_fn saved_flush;
  void GFX_PTR *saved_user;
  int ok;

  if (!path)
    return 0;
  dos_shot_file = fopen(path, "wb");
  if (!dos_shot_file)
    return 0;
  dos_shot_failed = 0;

  fprintf(dos_shot_file, "MRSHOT1 %d %d %lu\n",
          DOS_SCREEN_W, DOS_SCREEN_H,
          (unsigned long)DOS_FRAME_PIXELS *
              (unsigned long)sizeof(gfx_color_t));

  saved_flush = dos_renderer.flush;
  saved_user = dos_renderer.user;
  dos_renderer.flush = dos_shot_flush;
  dos_renderer.user = 0;
  gfx_render_tiled(&dos_renderer, draw_stress_scene, &stress,
                   GFX_RGB565_BLACK);
  dos_renderer.flush = saved_flush;
  dos_renderer.user = saved_user;

  ok = !dos_shot_failed;
  if (fclose(dos_shot_file) != 0)
    ok = 0;
  dos_shot_file = 0;
  return ok;
}

static int dos_write_report(const char *path, int sprite_count,
                            unsigned long frames, unsigned long elapsed_us,
                            int no_vsync) {
  FILE *f;
  double secs;

  if (!path)
    return 0;
  f = fopen(path, "wb");
  if (!f)
    return 0;

  secs = (double)elapsed_us / 1000000.0;
  fprintf(f, "platform=dos\n");
  fprintf(f, "demo=stress\n");
  fprintf(f, "mode=%s\n", MR_DOS_PRESENT_MODE == 0 ? "raw" : "tiled");
  fprintf(f, "width=%d\nheight=%d\n", DOS_SCREEN_W, DOS_SCREEN_H);
  fprintf(f, "tile_h=%d\n", DOS_TILE_H);
  fprintf(f, "sprites=%d\n", sprite_count);
  fprintf(f, "vsync=%d\n", no_vsync ? 0 : 1);
  fprintf(f, "frames=%lu\n", frames);
  fprintf(f, "elapsed_s=%.4f\n", secs);
  fprintf(f, "fps_avg=%.2f\n",
          secs > 0.0 ? (double)frames / secs : 0.0);
  fprintf(f, "sim_ticks=%lu\n", frames);
  fprintf(f, "sim_hz=%.2f\n",
          secs > 0.0 ? (double)frames / secs : 0.0);
  fclose(f);
  return 1;
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
  printf("               [/notile] [/nocollide] [/shot=FILE] [/report=FILE]\n");
  printf("Examples:\n");
  printf("  mstress /sprites 512 /frames 2100 /novsync\n");
  printf("  mstress /sprites 512 /frames 2000 /shot=dos.bin /report=dos.txt\n");
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
  unsigned long start_us;
  unsigned long end_us;
  int frame_limit;
  int no_vsync;
  const char *shot_path;
  const char *report_path;
  int i;

  frame_limit = 2100;
  no_vsync = MR_DOS_VSYNC ? 0 : 1;
  shot_path = 0;
  report_path = 0;

  mr_stress_config_defaults(&cfg, DOS_SCREEN_W, DOS_SCREEN_H);
  cfg.sprite_count = 512;
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
    } else if ((strcmp(argv[i], "/shot") == 0 ||
                strcmp(argv[i], "-shot") == 0) &&
               i + 1 < argc) {
      ++i;
      shot_path = argv[i];
    } else if (strncmp(argv[i], "/shot=", 6) == 0 ||
               strncmp(argv[i], "-shot=", 6) == 0) {
      shot_path = argv[i] + 6;
    } else if ((strcmp(argv[i], "/report") == 0 ||
                strcmp(argv[i], "-report") == 0) &&
               i + 1 < argc) {
      ++i;
      report_path = argv[i];
    } else if (strncmp(argv[i], "/report=", 8) == 0 ||
               strncmp(argv[i], "-report=", 8) == 0) {
      report_path = argv[i] + 8;
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

  printf("MicroRender 320x240 RGB565 Mode X stress: mode=%s sprites=%d frames=%d vsync=%s flush=%s "
         "statsrate=%d\n",
         MR_DOS_PRESENT_MODE == 0 ? "raw" : "tiled", cfg.sprite_count,
         frame_limit, no_vsync ? "off" : "on",
         dos_vga_flush_enabled() ? "on" : "off", cfg.stats_sample_rate);
  printf("Press ESC during the run to stop early.\n");

#if MR_DOS_PRESENT_MODE == 0
  dos_raw_frame =
      (gfx_color_t __huge *)halloc(DOS_FRAME_PIXELS, sizeof(gfx_color_t));
  if (!dos_raw_frame) {
    printf("ERROR: raw DOS mode needs a 150 KiB huge-memory framebuffer.\n");
    return 1;
  }
  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, capture_raw_tile, 0);
#else
  gfx_init(&dos_renderer, DOS_SCREEN_W, DOS_SCREEN_H, dos_tile_buffer,
           DOS_TILE_H, dos_vga_flush_tile, 0);
#endif
  mr_stress_init(&stress, &cfg);

  dos_vga_enter();
  start_us = dos_vga_micros();
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
#if MR_DOS_PRESENT_MODE == 0
    gfx_render_tiled(&dos_renderer, draw_stress_scene, &stress, GFX_RGB565_BLACK);
    dos_vga_present_rgb565_frame(dos_raw_frame);
#else
    gfx_render_tiled_no_clear(&dos_renderer, draw_stress_scene, &stress);
#endif
    delay(250);
    dos_vga_set_flush_enabled(saved_flush);
    start_us = dos_vga_micros();
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

    /* Stress is a frame-coupled workload by design: one deterministic update
       for each frame the target manages to render. */
    mr_stress_tick(&stress);
#if MR_DOS_PRESENT_MODE == 0
    /* Raw reference: clear/draw every logical pixel, then upload the whole
       completed frame. No tile is presented while rasterization is active. */
    gfx_render_tiled(&dos_renderer, draw_stress_scene, &stress, GFX_RGB565_BLACK);
    dos_vga_present_rgb565_frame(dos_raw_frame);
#else
    gfx_render_tiled_no_clear(&dos_renderer, draw_stress_scene, &stress);
#endif
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

  end_us = dos_vga_micros();
  end_tick = dos_vga_ticks();
  mr_stress_get_metrics(&stress, &m);

  if (shot_path)
    (void)dos_write_shot(shot_path);

  dos_vga_leave();

  if (report_path)
    (void)dos_write_report(report_path, cfg.sprite_count, frames,
                           end_us - start_us, no_vsync);
#if MR_DOS_PRESENT_MODE == 0
  hfree(dos_raw_frame);
  dos_raw_frame = 0;
#endif

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
