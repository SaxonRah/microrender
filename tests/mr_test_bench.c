/* MicroRender host benchmark.
 *
 * This measures the renderer core in isolation on the host: no LCD, no VGA, no
 * DOSBox. That makes it useless as a prediction of hardware framerate and very
 * useful for the thing it is actually for, which is comparing renderer
 * configurations against each other under identical conditions.
 *
 * Linear-scan RLE is highly machine-dependent: it can lose to or roughly
 * tie the plain per-pixel colorkey path because each tile re-walks every run
 * in the sprite. The row-start index is the robust optimisation being measured.
 *
 * Usage: mr_test_bench [frames]
 */
#include "mr_test_support.h"
#include <time.h>

int mrt_checks = 0;
int mrt_failures = 0;

#define W 320
#define H 240

#define SW 32
#define SH 32
#define KEY ((gfx_color_t)0x0000u)

static gfx_color_t g_spr[SW * SH];
static gfx_color_t g_pool[SW * SH];
static gfx_rle_run_t g_runs[SW * SH];
static uint16_t g_rows[SH + 1];

static gfx_sprite_t s_opaque, s_key, s_rle, s_rle_idx;

static const gfx_sprite_t *g_cur;
static int g_sprites_per_frame = 512;

static void scene(gfx_renderer_t *r, void *user) {
  int i;
  (void)user;
  /* Fixed, coprime strides: same sprite positions for every configuration, so
     the comparison is not perturbed by different overlap patterns. */
  for (i = 0; i < g_sprites_per_frame; ++i) {
    int x = (i * 37) % (W - SW);
    int y = (i * 53) % (H - SH);
    gfx_blit(r, g_cur, x, y);
  }
}

static double now_seconds(void) {
  return (double)clock() / (double)CLOCKS_PER_SEC;
}

static double bench_path(const gfx_sprite_t *s, int tile_h, int frames,
                         mrt_fb_t *fb, gfx_color_t *tile) {
  gfx_renderer_t r;
  double t0;
  int f;

  g_cur = s;
  gfx_init(&r, W, H, tile, tile_h, mrt_fb_flush, fb);

  gfx_render_tiled(&r, scene, NULL, (gfx_color_t)0x0000u); /* warm caches */

  t0 = now_seconds();
  for (f = 0; f < frames; ++f)
    gfx_render_tiled(&r, scene, NULL, (gfx_color_t)0x0000u);
  return (double)frames / (now_seconds() - t0);
}

static void bench_blit_paths(mrt_fb_t *fb, gfx_color_t *tile, int frames) {
  double f_opaque, f_key, f_rle, f_idx;

  f_opaque = bench_path(&s_opaque, 16, frames, fb, tile);
  f_key = bench_path(&s_key, 16, frames, fb, tile);
  f_rle = bench_path(&s_rle, 16, frames, fb, tile);
  f_idx = bench_path(&s_rle_idx, 16, frames, fb, tile);

  printf("Blit paths (%d sprites/frame, %dx%d target, 16px tiles)\n\n",
         g_sprites_per_frame, W, H);
  printf("  %-24s %10.1f fps   %5.2fx\n", "raw opaque (memcpy)", f_opaque,
         f_opaque / f_key);
  printf("  %-24s %10.1f fps   %5.2fx\n", "colorkey per-pixel", f_key, 1.0);
  printf("  %-24s %10.1f fps   %5.2fx\n", "RLE, linear run scan", f_rle,
         f_rle / f_key);
  printf("  %-24s %10.1f fps   %5.2fx   (%d runs)\n", "RLE + row-start index",
         f_idx, f_idx / f_key, s_rle_idx.run_count);
  printf("\n  Baseline is the colorkey path. ");
  if (f_rle < f_key) {
    printf("Linear-scan RLE was %.1f%% slower on this run.\n",
           (1.0 - f_rle / f_key) * 100.0);
  } else {
    printf("Linear-scan RLE was %.1f%% faster on this run.\n",
           (f_rle / f_key - 1.0) * 100.0);
  }
  printf("  Without the row index each tile still re-walks all %d runs; the\n"
         "  indexed path is %.2fx faster than the linear scan here.\n\n",
         s_rle.run_count, f_idx / f_rle);
}

static void bench_tile_heights(mrt_fb_t *fb, gfx_color_t *tile, int frames) {
  static const int heights[] = {4, 8, 16, 24, 40, 60, 120, 240};
  size_t i;

  printf("Tile height sweep (RLE + row index, %d sprites/frame)\n\n",
         g_sprites_per_frame);
  for (i = 0; i < sizeof(heights) / sizeof(heights[0]); ++i) {
    int th = heights[i];
    double fps;
    gfx_renderer_t r;
    /* Capacity must cover the taller tiles; the buffer is allocated for the
       largest height in the sweep. */
    gfx_init(&r, W, H, tile, th, mrt_fb_flush, fb);
    fps = bench_path(&s_rle_idx, th, frames, fb, tile);
    printf("  %3d rows  %6ld bytes/tile  %10.1f fps\n", th,
           (long)((long)W * th * (long)sizeof(gfx_color_t)), fps);
  }
  printf("\n  Taller tiles mean fewer flushes and fewer per-tile sprite\n"
         "  rejections, at the cost of RAM. 16 rows is 10 KiB at 320px wide,\n"
         "  which is the largest tile that fits a single 64 KiB DOS segment\n"
         "  alongside everything else.\n\n");
}

int main(int argc, char **argv) {
  mrt_fb_t fb;
  gfx_color_t *tile;
  int frames = (argc > 1) ? atoi(argv[1]) : 300;
  int transparent = 0, i;

  if (frames <= 0)
    frames = 300;

  /* Circular sprite: a realistic character silhouette, not a solid block. */
  for (i = 0; i < SW * SH; ++i) {
    int x = i % SW, y = i / SW;
    int dx = x - SW / 2, dy = y - SH / 2;
    if (dx * dx + dy * dy > (SW / 2) * (SW / 2)) {
      g_spr[i] = KEY;
      ++transparent;
    } else {
      g_spr[i] = (gfx_color_t)(0x1000u + (unsigned)i);
    }
  }

  mrt_sprite_raw(&s_opaque, g_spr, SW, SH, KEY, 0u);
  mrt_sprite_raw(&s_key, g_spr, SW, SH, KEY, GFX_SPRITE_COLORKEY);
  gfx_rle_build_from_colorkey(g_spr, SW, SH, KEY, g_runs, SW * SH, g_pool,
                              SW * SH, &s_rle);
  gfx_rle_build_from_colorkey_indexed(g_spr, SW, SH, KEY, g_runs, SW * SH,
                                      g_pool, SW * SH, g_rows, SH + 1,
                                      &s_rle_idx);

  mrt_fb_init(&fb, W, H);
  tile = (gfx_color_t *)malloc(sizeof(gfx_color_t) * W * H);
  memset(tile, 0, sizeof(gfx_color_t) * W * H);

  printf("MicroRender host benchmark  (%d frames per configuration)\n",
         frames);
  printf("sprite %dx%d, %d%% transparent\n\n", SW, SH,
         transparent * 100 / (SW * SH));

  bench_blit_paths(&fb, tile, frames);
  bench_tile_heights(&fb, tile, frames);

  printf("Host-only measurement. Not a DOS or Pico framerate prediction.\n");

  free(tile);
  mrt_fb_free(&fb);
  return 0;
}
