/* MicroRender randomised stress harness.
 *
 * Every drawing entry point is hit with coordinates far outside the screen, on
 * both sides of tile seams, through randomised clip windows and sub-region
 * (dirty-rect) passes. Built with -fsanitize=address,undefined by the CMake
 * host target, so any clipping arithmetic that lets a write escape the tile
 * buffer aborts the run.
 *
 * The flush callback independently re-validates every rect the renderer hands
 * it, so a bad tile rect is caught even where it would not have overflowed.
 *
 * Usage: mr_test_fuzz [iterations] [seed]
 * Exit status is non-zero if any flush rect was out of range.
 */
#include "mr_test_support.h"

int mrt_checks = 0;
int mrt_failures = 0;

#define W 320
#define H 240
#define TH 16

#define SW 32
#define SH 32
#define KEY ((gfx_color_t)0x0000u)

static gfx_color_t g_spr[SW * SH];
static gfx_color_t g_pool[SW * SH];
static gfx_rle_run_t g_runs[SW * SH];
static uint16_t g_rows[SH + 1];
static gfx_sprite_t g_sprites[4];

static mrt_rng_t g_rng;

static void build_sprites(void) {
  mrt_make_pattern(g_spr, SW, SH, KEY, 5);
  mrt_sprite_raw(&g_sprites[0], g_spr, SW, SH, KEY, 0u);
  mrt_sprite_raw(&g_sprites[1], g_spr, SW, SH, KEY, GFX_SPRITE_COLORKEY);
  gfx_rle_build_from_colorkey(g_spr, SW, SH, KEY, g_runs, SW * SH, g_pool,
                              SW * SH, &g_sprites[2]);
  gfx_rle_build_from_colorkey_indexed(g_spr, SW, SH, KEY, g_runs, SW * SH,
                                      g_pool, SW * SH, g_rows, SH + 1,
                                      &g_sprites[3]);
}

/* Draw everything the renderer exposes, at hostile coordinates. */
static void scene(gfx_renderer_t *r, void *user) {
  int n = *(int *)user;
  int i;
  for (i = 0; i < n; ++i) {
    gfx_blit(r, &g_sprites[i & 3], mrt_rand(&g_rng, -4000, 4000),
             mrt_rand(&g_rng, -4000, 4000));
    gfx_fill_rect(r, mrt_rand(&g_rng, -500, 500), mrt_rand(&g_rng, -500, 500),
                  mrt_rand(&g_rng, -50, 400), mrt_rand(&g_rng, -50, 400),
                  (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
    gfx_draw_rect(r, mrt_rand(&g_rng, -500, 500), mrt_rand(&g_rng, -500, 500),
                  mrt_rand(&g_rng, -50, 400), mrt_rand(&g_rng, -50, 400),
                  (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
    gfx_draw_line(r, mrt_rand(&g_rng, -900, 900), mrt_rand(&g_rng, -900, 900),
                  mrt_rand(&g_rng, -900, 900), mrt_rand(&g_rng, -900, 900),
                  (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
    gfx_draw_hline(r, mrt_rand(&g_rng, -500, 500), mrt_rand(&g_rng, -500, 500),
                   mrt_rand(&g_rng, -100, 500),
                   (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
    gfx_draw_vline(r, mrt_rand(&g_rng, -500, 500), mrt_rand(&g_rng, -500, 500),
                   mrt_rand(&g_rng, -100, 500),
                   (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
    gfx_draw_pixel(r, mrt_rand(&g_rng, -900, 900), mrt_rand(&g_rng, -900, 900),
                   (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
#if GFX_ENABLE_TRIANGLES
    gfx_fill_triangle(r, mrt_rand(&g_rng, -900, 900),
                      mrt_rand(&g_rng, -900, 900), mrt_rand(&g_rng, -900, 900),
                      mrt_rand(&g_rng, -900, 900), mrt_rand(&g_rng, -900, 900),
                      mrt_rand(&g_rng, -900, 900),
                      (gfx_color_t)mrt_rand(&g_rng, 0, 0xFFFF));
#endif
    gfx_draw_text5x7(r, mrt_rand(&g_rng, -200, 400), mrt_rand(&g_rng, -200, 400),
                     "MicroRender \x01\x7f\xff~", (gfx_color_t)0xFFFFu,
                     mrt_rand(&g_rng, 1, 4));
  }
}

/* A scene that also randomises the clip window per tile, since clip is
   per-tile state and the blitters intersect against it separately. */
static void scene_clipped(gfx_renderer_t *r, void *user) {
  gfx_set_clip(r, mrt_rand(&g_rng, -50, 350), mrt_rand(&g_rng, -50, 350),
               mrt_rand(&g_rng, -10, 400), mrt_rand(&g_rng, -10, 400));
  scene(r, user);
  gfx_reset_clip(r);
}

/* Deliberately malformed sprite data, of the kind a truncated or corrupt .MRP
   could produce. gfx_sprite_rle_validate() is what a loader should use to
   reject these; this checks that it does, and that nothing reaches the
   blitters unvalidated. */
static int hostile_data_is_rejected(void) {
  gfx_sprite_t s;
  gfx_rle_run_t runs[2];
  uint16_t rows[4];
  int rejected = 0, total = 0;

  memset(&s, 0, sizeof s);
  s.width = 8;
  s.height = 3;
  s.pixels = g_spr;
  s.flags = GFX_SPRITE_RLE;
  s.runs = runs;
  s.run_count = 2;
  runs[0].x = 0; runs[0].y = 0; runs[0].len = 8; runs[0].offset = 0;
  runs[1].x = 100; runs[1].y = 99; runs[1].len = 3000; runs[1].offset = 0;
  ++total;
  rejected += (gfx_sprite_rle_validate(&s, SW * SH) == 0);

  s.flags = GFX_SPRITE_RLE | GFX_SPRITE_RLE_ROWSTART;
  s.row_start = rows;
  rows[0] = 0; rows[1] = 900; rows[2] = 901; rows[3] = 65535;
  ++total;
  rejected += (gfx_sprite_rle_validate(&s, SW * SH) == 0);

  runs[1].x = 0; runs[1].y = 1; runs[1].len = 8; runs[1].offset = 0;
  rows[0] = 0; rows[1] = 1; rows[2] = 2; rows[3] = 2;
  s.height = 3;
  ++total;
  rejected += (gfx_sprite_rle_validate(&s, 4) == 0); /* pool too small */

  return rejected == total;
}

int main(int argc, char **argv) {
  gfx_renderer_t r;
  mrt_fb_t fb;
  gfx_color_t *tile;
  gfx_color_t *tile_b;
  int iterations = (argc > 1) ? atoi(argv[1]) : 300;
  unsigned long long seed = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x5EEDu;
  int n = 12;
  int frame;

  if (iterations <= 0)
    iterations = 300;

  mrt_rng_seed(&g_rng, seed);
  build_sprites();

  mrt_fb_init(&fb, W, H);
  tile = (gfx_color_t *)malloc(sizeof(gfx_color_t) * W * TH);
  tile_b = (gfx_color_t *)malloc(sizeof(gfx_color_t) * W * TH);
  memset(tile, 0, sizeof(gfx_color_t) * W * TH);
  memset(tile_b, 0, sizeof(gfx_color_t) * W * TH);

  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);

  printf("MicroRender fuzz harness: %d iterations, seed 0x%llx\n", iterations,
         seed);

  for (frame = 0; frame < iterations; ++frame) {
    /* full-screen tiled pass */
    gfx_render_tiled(&r, scene, &n, (gfx_color_t)0x0000u);
    /* sub-region pass, the path dirty-rect rendering uses */
    gfx_render_tiled_region(&r, mrt_rand(&g_rng, -100, 400),
                            mrt_rand(&g_rng, -100, 400),
                            mrt_rand(&g_rng, -20, 400),
                            mrt_rand(&g_rng, -20, 400), scene, &n,
                            (gfx_color_t)0x1111u);
    /* randomised clip windows */
    gfx_render_tiled(&r, scene_clipped, &n, (gfx_color_t)0x2222u);
    /* pipelined double-buffered path */
    gfx_render_tiled_pipelined(&r, tile_b, scene, &n, (gfx_color_t)0x3333u,
                               GFX_RENDER_CLEAR);
  }

  /* dirty-rect list driven rendering */
  {
    gfx_dirty_list_t d;
    int i;
    gfx_dirty_init(&d, W, H);
    for (i = 0; i < 64; ++i)
      gfx_dirty_add_sprite_move(&d, mrt_rand(&g_rng, -300, 600),
                                mrt_rand(&g_rng, -300, 600),
                                mrt_rand(&g_rng, -300, 600),
                                mrt_rand(&g_rng, -300, 600), SW, SH, 2);
    gfx_dirty_merge(&d);
    gfx_dirty_render(&r, &d, scene, &n, (gfx_color_t)0x0000u, GFX_RENDER_CLEAR);
  }

  MRT_CHECK_EQ_INT(fb.rejected_flushes, 0,
                   "flush rects outside the framebuffer");
  MRT_CHECK(hostile_data_is_rejected(),
            "gfx_sprite_rle_validate must reject malformed run tables");

  printf("\n%ld flushes, %ld pixels written, %ld out-of-range flush rects\n",
         fb.flush_calls, fb.pixels_written, fb.rejected_flushes);
  printf("%d checks, %d failures\n", mrt_checks, mrt_failures);

  free(tile);
  free(tile_b);
  mrt_fb_free(&fb);
  return mrt_failures == 0 ? 0 : 1;
}
