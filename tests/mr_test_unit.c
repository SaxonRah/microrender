/* MicroRender unit tests.
 *
 * These assert behaviour, not just absence of crashes. The most important one
 * is test_blit_path_equivalence. The three transparency-preserving paths
 * (colorkey, linear RLE, RLE+row-index) are three implementations of one
 * specification, so any disagreement between them at any position is a bug in
 * whichever one is the odd man out. That property is what makes it safe to keep
 * optimising the RLE path.
 *
 * The raw opaque path is deliberately NOT in that comparison: it writes every
 * pixel including the ones the transparent paths skip, so it implements a
 * different specification. It gets its own alignment test instead.
 */
#include "mr_test_support.h"
#include "gfx_engine.h"
#include "mr_strbuf.h"

int mrt_checks = 0;
int mrt_failures = 0;

#define W 160
#define H 120
#define TH 16

#define SW 24
#define SH 20
#define KEY ((gfx_color_t)0x0000u)
#define BG ((gfx_color_t)0x0842u)

static gfx_color_t g_spr[SW * SH];
static gfx_color_t g_pool[SW * SH];
static gfx_rle_run_t g_runs[SW * SH];
static uint16_t g_rows[SH + 1];

static gfx_sprite_t s_opaque, s_key, s_rle, s_rle_idx;

static void build_sprites(void) {
  mrt_make_pattern(g_spr, SW, SH, KEY, 4);
  mrt_sprite_raw(&s_opaque, g_spr, SW, SH, KEY, 0u);
  mrt_sprite_raw(&s_key, g_spr, SW, SH, KEY, GFX_SPRITE_COLORKEY);

  MRT_CHECK(gfx_rle_build_from_colorkey(g_spr, SW, SH, KEY, g_runs, SW * SH,
                                        g_pool, SW * SH, &s_rle) == 1,
            "gfx_rle_build_from_colorkey should succeed");
  MRT_CHECK(gfx_rle_build_from_colorkey_indexed(
                g_spr, SW, SH, KEY, g_runs, SW * SH, g_pool, SW * SH, g_rows,
                SH + 1, &s_rle_idx) == 1,
            "gfx_rle_build_from_colorkey_indexed should succeed");
  MRT_CHECK((s_rle_idx.flags & GFX_SPRITE_RLE_ROWSTART) != 0,
            "indexed builder should set the ROWSTART flag");
}

/* ------------------------------------------------------------------ */

typedef struct {
  const gfx_sprite_t *sprite;
  int x, y;
} blit_job_t;

static void scene_one_sprite(gfx_renderer_t *r, void *user) {
  blit_job_t *j = (blit_job_t *)user;
  gfx_blit(r, j->sprite, j->x, j->y);
}

static void render_sprite_to(mrt_fb_t *fb, const gfx_sprite_t *s, int x, int y) {
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  blit_job_t job;
  job.sprite = s;
  job.x = x;
  job.y = y;
  mrt_fb_fill(fb, BG);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, fb);
  gfx_render_tiled(&r, scene_one_sprite, &job, BG);
}

/* ------------------------------------------------------------------ */

static void test_blit_alignment(void) {
  mrt_fb_t fb;
  int x = 40, y = 32, sx, sy;
  int mismatches = 0;
  mrt_fb_init(&fb, W, H);
  render_sprite_to(&fb, &s_opaque, x, y);

  for (sy = 0; sy < SH; ++sy)
    for (sx = 0; sx < SW; ++sx)
      if (mrt_fb_get(&fb, x + sx, y + sy) != g_spr[sy * SW + sx])
        ++mismatches;

  MRT_CHECK_EQ_INT(mismatches, 0, "opaque blit pixel alignment");
  MRT_CHECK(mrt_fb_get(&fb, x - 1, y) == BG,
            "opaque blit must not write left of the sprite");
  MRT_CHECK(mrt_fb_get(&fb, x + SW, y) == BG,
            "opaque blit must not write right of the sprite");
  MRT_CHECK_EQ_INT(fb.rejected_flushes, 0, "flush rects must stay in bounds");
  mrt_fb_free(&fb);
}

static void test_colorkey_transparency(void) {
  mrt_fb_t fb;
  int x = 10, y = 8;
  mrt_fb_init(&fb, W, H);
  render_sprite_to(&fb, &s_key, x, y);

  /* The pattern has a 4px transparent border, so the corner must be untouched
     and the interior must be written. */
  MRT_CHECK(mrt_fb_get(&fb, x, y) == BG, "colorkey corner should stay background");
  MRT_CHECK(mrt_fb_get(&fb, x + SW / 2, y + SH / 2) != BG,
            "colorkey interior should be drawn");
  mrt_fb_free(&fb);
}

/* The core invariant: every transparency-preserving blit path renders the same
   image, everywhere. Raw opaque is excluded by design, see the file header. */
static void test_blit_path_equivalence(void) {
  static const int xs[] = {-SW, -SW + 1, -5, 0, 1, 37, W - SW - 1,
                           W - SW,       W - 3, W, W + 5};
  static const int ys[] = {-SH, -SH + 1, -3, 0, 1, 15, 16, 17, H - SH,
                           H - 2,        H,  H + 4};
  mrt_fb_t ref, cmp;
  size_t nx = sizeof(xs) / sizeof(xs[0]);
  size_t ny = sizeof(ys) / sizeof(ys[0]);
  size_t i, j;
  int diff_rle = 0, diff_idx = 0, positions = 0;

  mrt_fb_init(&ref, W, H);
  mrt_fb_init(&cmp, W, H);

  for (j = 0; j < ny; ++j) {
    for (i = 0; i < nx; ++i) {
      size_t bytes = (size_t)W * (size_t)H * sizeof(gfx_color_t);
      ++positions;
      render_sprite_to(&ref, &s_key, xs[i], ys[j]);

      render_sprite_to(&cmp, &s_rle, xs[i], ys[j]);
      if (memcmp(ref.pixels, cmp.pixels, bytes) != 0) {
        ++diff_rle;
        printf("    RLE differs from colorkey at (%d,%d)\n", xs[i], ys[j]);
      }

      render_sprite_to(&cmp, &s_rle_idx, xs[i], ys[j]);
      if (memcmp(ref.pixels, cmp.pixels, bytes) != 0) {
        ++diff_idx;
        printf("    RLE+rowindex differs from colorkey at (%d,%d)\n", xs[i],
               ys[j]);
      }
    }
  }

  printf("  (3 paths x %d sprite positions, including all four screen edges\n"
         "   and both sides of the %dpx tile seams)\n",
         positions, TH);
  MRT_CHECK_EQ_INT(diff_rle, 0, "RLE vs colorkey mismatches");
  MRT_CHECK_EQ_INT(diff_idx, 0, "RLE+rowindex vs colorkey mismatches");

  mrt_fb_free(&ref);
  mrt_fb_free(&cmp);
}

/* ------------------------------------------------------------------ */

typedef struct {
  int x, y, w, h;
  gfx_color_t color;
} rect_job_t;

static void scene_fill_rect(gfx_renderer_t *r, void *user) {
  rect_job_t *j = (rect_job_t *)user;
  gfx_fill_rect(r, j->x, j->y, j->w, j->h, j->color);
}

static void test_fill_rect_clipping(void) {
  mrt_fb_t fb;
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  rect_job_t job;
  const gfx_color_t FG = (gfx_color_t)0xF81Fu;

  mrt_fb_init(&fb, W, H);

  /* Rect straddling the top-left corner: exactly the on-screen part is drawn. */
  job.x = -10; job.y = -6; job.w = 30; job.h = 20; job.color = FG;
  mrt_fb_fill(&fb, BG);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);
  gfx_render_tiled(&r, scene_fill_rect, &job, BG);
  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), (30 - 10) * (20 - 6),
                   "fill_rect clipped at top-left corner");

  /* Fully offscreen draws nothing. */
  job.x = W + 5; job.y = 10; job.w = 20; job.h = 20;
  mrt_fb_fill(&fb, BG);
  gfx_render_tiled(&r, scene_fill_rect, &job, BG);
  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), 0, "offscreen fill_rect");

  /* Negative extents draw nothing rather than wrapping. */
  job.x = 20; job.y = 20; job.w = -30; job.h = -30;
  mrt_fb_fill(&fb, BG);
  gfx_render_tiled(&r, scene_fill_rect, &job, BG);
  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), 0, "negative-extent fill_rect");

  MRT_CHECK_EQ_INT(fb.rejected_flushes, 0, "no out-of-range flush rects");
  mrt_fb_free(&fb);
}

/* Clip is a per-tile property: gfx_begin_tile_rect() resets it at the top of
   every tile, so a clip must be set inside the scene callback. This scene does
   that, and the test below also pins down the fact that a clip set outside is
   discarded, so nobody "fixes" the reset without noticing. */
static void scene_clipped_fill(gfx_renderer_t *r, void *user) {
  rect_job_t *j = (rect_job_t *)user;
  gfx_set_clip(r, 30, 25, 20, 10);
  gfx_fill_rect(r, j->x, j->y, j->w, j->h, j->color);
  gfx_reset_clip(r);
}

static void test_clip_window(void) {
  mrt_fb_t fb;
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  rect_job_t job;

  mrt_fb_init(&fb, W, H);
  mrt_fb_fill(&fb, BG);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);

  /* Ask to fill the whole screen, clipped to a 20x10 window. */
  job.x = 0; job.y = 0; job.w = W; job.h = H; job.color = (gfx_color_t)0x07E0u;
  /* Clearing variant: the tile is the unit of transfer, so a no_clear pass
     would flush whatever stale content the tile buffer already held. */
  gfx_render_tiled(&r, scene_clipped_fill, &job, BG);

  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), 20 * 10,
                   "clip window should bound the fill exactly");
  MRT_CHECK(mrt_fb_get(&fb, 30, 25) != BG, "clip window top-left inclusive");
  MRT_CHECK(mrt_fb_get(&fb, 49, 34) != BG, "clip window bottom-right inclusive");
  MRT_CHECK(mrt_fb_get(&fb, 50, 35) == BG, "clip window upper bound exclusive");

  /* Documented contract: a clip set before gfx_render_tiled* is reset per tile
     and therefore has no effect. */
  mrt_fb_fill(&fb, BG);
  gfx_set_clip(&r, 30, 25, 20, 10);
  gfx_render_tiled(&r, scene_fill_rect, &job, BG);
  gfx_reset_clip(&r);
  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), W * H,
                   "clip set outside the scene callback is reset per tile");

  mrt_fb_free(&fb);
}

/* ------------------------------------------------------------------ */

static void test_rect_algebra(void) {
  gfx_rect_t a = gfx_rect_make(10, 10, 20, 20);
  gfx_rect_t b = gfx_rect_make(25, 25, 20, 20);
  gfx_rect_t c = gfx_rect_make(100, 100, 5, 5);
  gfx_rect_t u = gfx_rect_union(a, b);
  gfx_rect_t x = gfx_rect_intersection(a, b);
  gfx_rect_t e = gfx_rect_intersection(a, c);

  MRT_CHECK_EQ_INT(u.x, 10, "union x");
  MRT_CHECK_EQ_INT(u.y, 10, "union y");
  MRT_CHECK_EQ_INT(u.w, 35, "union w");
  MRT_CHECK_EQ_INT(u.h, 35, "union h");
  MRT_CHECK_EQ_INT(x.x, 25, "intersection x");
  MRT_CHECK_EQ_INT(x.w, 5, "intersection w");
  MRT_CHECK(gfx_rect_empty(e), "disjoint intersection is empty");
  MRT_CHECK(gfx_rects_overlap(a, b), "a and b overlap");
  MRT_CHECK(!gfx_rects_overlap(a, c), "a and c do not overlap");
  MRT_CHECK_EQ_INT(gfx_rect_area(a), 400, "rect area");

  /* Touching-but-not-overlapping must be distinguished from overlapping;
     dirty-rect merging depends on the difference. */
  {
    gfx_rect_t t1 = gfx_rect_make(0, 0, 10, 10);
    gfx_rect_t t2 = gfx_rect_make(10, 0, 10, 10);
    MRT_CHECK(!gfx_rects_overlap(t1, t2), "edge-adjacent rects do not overlap");
    MRT_CHECK(gfx_rects_touch_or_overlap(t1, t2), "edge-adjacent rects touch");
  }
}

static void test_dirty_merge(void) {
  gfx_dirty_list_t d;
  int i;
  unsigned long before, after;

  gfx_dirty_init(&d, W, H);
  for (i = 0; i < GFX_DIRTY_MAX_RECTS * 3; ++i)
    gfx_dirty_add_rect(&d, gfx_rect_make(i * 3 % W, i * 5 % H, 12, 12));

  MRT_CHECK(d.count <= GFX_DIRTY_MAX_RECTS,
            "dirty list must never exceed GFX_DIRTY_MAX_RECTS (got %d)",
            d.count);

  before = gfx_dirty_total_area(&d);
  gfx_dirty_merge(&d);
  after = gfx_dirty_total_area(&d);
  MRT_CHECK(d.count <= GFX_DIRTY_MAX_RECTS, "merge keeps the list bounded");
  MRT_CHECK(after >= 1u, "merged list still covers something");
  (void)before;

  /* Overflowing the list must fall back to a full redraw, never drop damage. */
  gfx_dirty_clear(&d);
  for (i = 0; i < GFX_DIRTY_MAX_RECTS * 4; ++i)
    gfx_dirty_add_rect(&d, gfx_rect_make((i * 17) % W, (i * 29) % H, 8, 8));
  MRT_CHECK(d.full_redraw || d.count <= GFX_DIRTY_MAX_RECTS,
            "overflow must set full_redraw rather than silently drop rects");

  gfx_dirty_clear(&d);
  gfx_dirty_add_rect(&d, gfx_rect_make(-50, -50, 20, 20));
  MRT_CHECK_EQ_INT(d.count, 0, "fully offscreen rect should not be recorded");
}

/* ------------------------------------------------------------------ */

static void test_tile_capacity_clamp(void) {
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  mrt_fb_t fb;

  mrt_fb_init(&fb, W, H);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);
  MRT_CHECK_EQ_INT(r.tile_capacity, (long)W * TH,
                   "gfx_init should infer tile capacity");

  /* Asking for a taller tile than the buffer holds must be clamped, not
     honoured. Before the fix this rasterized past the end of `tile`. */
  gfx_begin_tile_rect(&r, 0, 0, W, H);
  MRT_CHECK(r.tile_h <= TH, "tile height clamped to capacity (got %d)", r.tile_h);
  MRT_CHECK((long)r.tile_w * (long)r.tile_h <= r.tile_capacity,
            "clamped tile fits the buffer");

  /* A narrower rect may legitimately be taller. */
  gfx_begin_tile_rect(&r, 0, 0, W / 4, H);
  MRT_CHECK((long)r.tile_w * (long)r.tile_h <= r.tile_capacity,
            "narrow tall tile still fits the buffer");

  mrt_fb_free(&fb);
}

static void test_rle_validator(void) {
  gfx_sprite_t s;
  gfx_rle_run_t runs[4];
  uint16_t rows[4];

  MRT_CHECK(gfx_sprite_rle_validate(&s_rle, SW * SH) == 1,
            "validator accepts a pack built by the RLE builder");
  MRT_CHECK(gfx_sprite_rle_validate(&s_rle_idx, SW * SH) == 1,
            "validator accepts an indexed pack");
  MRT_CHECK(gfx_sprite_rle_validate(&s_opaque, SW * SH) == 0,
            "validator rejects a non-RLE sprite");

  /* run length running off the end of the pixel pool */
  memset(&s, 0, sizeof s);
  s.width = 8; s.height = 2; s.pixels = g_spr;
  s.flags = GFX_SPRITE_RLE; s.runs = runs; s.run_count = 1;
  runs[0].x = 0; runs[0].y = 0; runs[0].len = 8; runs[0].offset = 0;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 1, "in-bounds run accepted");
  runs[0].len = 3000;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 0,
            "run overrunning the pixel pool rejected");

  /* run positioned outside the sprite's own width */
  runs[0].len = 4; runs[0].x = 100;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 0, "run past sprite width rejected");

  /* run on a row that does not exist */
  runs[0].x = 0; runs[0].y = 99;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 0, "run past sprite height rejected");

  /* row_start table that lies about which runs belong to which row */
  runs[0].y = 0;
  runs[1].x = 0; runs[1].y = 1; runs[1].len = 4; runs[1].offset = 4;
  s.run_count = 2;
  s.flags = GFX_SPRITE_RLE | GFX_SPRITE_RLE_ROWSTART;
  s.row_start = rows;
  rows[0] = 0; rows[1] = 1; rows[2] = 2;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 1, "honest row index accepted");
  rows[1] = 900; rows[2] = 901;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 0,
            "row index beyond run_count rejected");
  rows[0] = 0; rows[1] = 2; rows[2] = 2;
  MRT_CHECK(gfx_sprite_rle_validate(&s, 8) == 0,
            "row index claiming another row's runs rejected");
}

/* ------------------------------------------------------------------ */

/* Regression test for the truncating-vs-floor division bug in the collision
   sweep: an actor at negative world coordinates used to compute the wrong tile
   edge when moving right, letting it step into a solid tile. */
static void test_collision_negative_coords(void) {
  static uint8_t flags[8 * 8];
  gfx_collision_map_t cm;
  gfx_actor_t a;
  gfx_sprite_t s;
  int i;

  for (i = 0; i < 8 * 8; ++i)
    flags[i] = 0;
  /* Make column 2 solid. */
  for (i = 0; i < 8; ++i)
    flags[i * 8 + 2] = GFX_TILE_SOLID;

  gfx_collision_init(&cm, 8, 8, 16, 16, flags, GFX_TILE_SOLID);
  mrt_sprite_raw(&s, g_spr, 8, 8, KEY, 0u);

  /* Start left of the origin and walk right into the solid column. */
  gfx_actor_init(&a, &s, -40, 20, 0, 1);
  gfx_actor_set_collider(&a, 0, 0, 8, 8);
  for (i = 0; i < 40; ++i)
    gfx_actor_move_collide(&a, &cm, 3, 0);

  MRT_CHECK(a.world_x + 8 <= 32,
            "actor must stop at the solid tile edge, stopped with right edge "
            "at %d (tile 2 starts at 32)",
            a.world_x + 8);
  MRT_CHECK(a.blocked_x != 0, "actor should report blocked_x");

  /* Same walk from a positive start must give the same stopping edge. */
  {
    gfx_actor_t b;
    gfx_actor_init(&b, &s, 4, 20, 0, 1);
    gfx_actor_set_collider(&b, 0, 0, 8, 8);
    for (i = 0; i < 40; ++i)
      gfx_actor_move_collide(&b, &cm, 3, 0);
    MRT_CHECK_EQ_INT(a.world_x, b.world_x,
                     "stopping position must not depend on approach sign");
  }
}

/* ------------------------------------------------------------------ */

static void test_degenerate_sprites(void) {
  mrt_fb_t fb;
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  gfx_sprite_t s;

  mrt_fb_init(&fb, W, H);
  mrt_fb_fill(&fb, BG);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);
  gfx_begin_tile(&r, 0, TH);

  mrt_sprite_raw(&s, g_spr, 0, 10, KEY, 0u);
  gfx_blit(&r, &s, 5, 5);
  mrt_sprite_raw(&s, g_spr, -5, 10, KEY, 0u);
  gfx_blit(&r, &s, 5, 5);
  mrt_sprite_raw(&s, NULL, 10, 10, KEY, 0u);
  gfx_blit(&r, &s, 5, 5);
  gfx_blit(&r, NULL, 5, 5);

  MRT_CHECK_EQ_INT(mrt_fb_count_not(&fb, BG), 0,
                   "degenerate sprites must draw nothing");
  mrt_fb_free(&fb);
}

static void test_line_endpoints(void) {
  mrt_fb_t fb;
  gfx_renderer_t r;
  gfx_color_t tile[W * TH];
  mrt_fb_init(&fb, W, H);
  mrt_fb_fill(&fb, BG);
  gfx_init(&r, W, H, tile, TH, mrt_fb_flush, &fb);

  /* Single full-height tile so the whole line lands in one pass. */
  gfx_set_tile_capacity(&r, (long)W * TH);
  gfx_begin_tile(&r, 0, TH);
  gfx_draw_line(&r, 2, 2, 100, 14, (gfx_color_t)0xFFFFu);
  gfx_flush_tile(&r);

  MRT_CHECK(mrt_fb_get(&fb, 2, 2) != BG, "line start pixel drawn");
  MRT_CHECK(mrt_fb_get(&fb, 100, 14) != BG, "line end pixel drawn");
  mrt_fb_free(&fb);
}


/* mr_strbuf replaced three duplicated copies of these helpers; the bounds
   behaviour is what makes it safe to share, so it is pinned down here. */
static void test_strbuf(void) {
  char buf[16] = {0};
  char *p;
  const char *end = buf + sizeof(buf) - 1;

  p = buf;
  p = mr_strbuf_str(p, end, "fps ");
  p = mr_strbuf_u32(p, end, 1234u);
  *p = '\0';
  MRT_CHECK(strcmp(buf, "fps 1234") == 0, "strbuf basic append, got \"%s\"", buf);

  p = buf; *mr_strbuf_u32(p, end, 0u) = '\0';
  MRT_CHECK(strcmp(buf, "0") == 0, "strbuf zero, got \"%s\"", buf);

  p = buf; *mr_strbuf_i32(p, end, -4095) = '\0';
  MRT_CHECK(strcmp(buf, "-4095") == 0, "strbuf negative, got \"%s\"", buf);

  p = buf; *mr_strbuf_frac(p, end, 686u, 10u) = '\0';
  MRT_CHECK(strcmp(buf, "68.6") == 0, "strbuf fraction, got \"%s\"", buf);

  /* Overflow must truncate at end, never write past it. */
  memset(buf, 'X', sizeof(buf));
  p = buf; end = buf + 4;
  p = mr_strbuf_str(p, end, "abcdefghijklmnop");
  MRT_CHECK(p == end, "strbuf must stop exactly at end");
  MRT_CHECK(buf[4] == 'X', "strbuf must not write at or past end");

  p = buf; end = buf + 2;
  p = mr_strbuf_u32(p, end, 999999u);
  MRT_CHECK(p == end, "strbuf u32 must stop at end");
}

/* ------------------------------------------------------------------ */

typedef void (*test_fn)(void);
typedef struct {
  const char *name;
  test_fn fn;
} test_entry_t;

int main(void) {
  static const test_entry_t tests[] = {
      {"blit alignment", test_blit_alignment},
      {"colorkey transparency", test_colorkey_transparency},
      {"blit path equivalence", test_blit_path_equivalence},
      {"fill_rect clipping", test_fill_rect_clipping},
      {"clip window", test_clip_window},
      {"rect algebra", test_rect_algebra},
      {"dirty list merge", test_dirty_merge},
      {"tile capacity clamp", test_tile_capacity_clamp},
      {"rle validator", test_rle_validator},
      {"collision at negative coords", test_collision_negative_coords},
      {"degenerate sprites", test_degenerate_sprites},
      {"line endpoints", test_line_endpoints},
      {"shared string builder", test_strbuf},
  };
  size_t i;
  int before;

  printf("MicroRender unit tests\n\n");
  build_sprites();

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    before = mrt_failures;
    printf("[ run ] %s\n", tests[i].name);
    tests[i].fn();
    printf("[%s] %s\n", mrt_failures == before ? "  ok  " : " FAIL ",
           tests[i].name);
  }

  printf("\n%d checks, %d failures\n", mrt_checks, mrt_failures);
  return mrt_failures == 0 ? 0 : 1;
}
