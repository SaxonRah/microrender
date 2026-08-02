/* MicroRender host test support.
 *
 * A framebuffer-backed flush target plus a tiny assertion and RNG layer, shared
 * by the unit, fuzz and benchmark binaries. Host-only: this file is never
 * compiled into a DOS or Pico build.
 */
#ifndef MR_TEST_SUPPORT_H
#define MR_TEST_SUPPORT_H

#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These helpers live in a header shared by several test binaries, so not every
   translation unit uses every one of them. */
#if defined(__GNUC__) || defined(__clang__)
#define MRT_UNUSED __attribute__((unused))
#else
#define MRT_UNUSED
#endif

/* ------------------------------------------------------------------ */
/* assertions                                                          */
/* ------------------------------------------------------------------ */

extern int mrt_checks;
extern int mrt_failures;

#define MRT_CHECK(cond, ...)                                                   \
  do {                                                                         \
    ++mrt_checks;                                                              \
    if (!(cond)) {                                                             \
      ++mrt_failures;                                                          \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);                            \
      printf(__VA_ARGS__);                                                     \
      printf("\n");                                                            \
    }                                                                          \
  } while (0)

#define MRT_CHECK_EQ_INT(got, want, what)                                      \
  MRT_CHECK((long)(got) == (long)(want), "%s: got %ld, want %ld", (what),       \
            (long)(got), (long)(want))

/* ------------------------------------------------------------------ */
/* deterministic RNG (never rand(); results must reproduce in CI)      */
/* ------------------------------------------------------------------ */

typedef struct {
  unsigned long long s;
} mrt_rng_t;

static MRT_UNUSED void mrt_rng_seed(mrt_rng_t *g, unsigned long long seed) {
  g->s = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static MRT_UNUSED int mrt_rand(mrt_rng_t *g, int lo, int hi) {
  g->s = g->s * 6364136223846793005ULL + 1442695040888963407ULL;
  if (hi <= lo)
    return lo;
  return lo + (int)((g->s >> 33) % (unsigned long long)(hi - lo + 1));
}

/* ------------------------------------------------------------------ */
/* framebuffer flush target                                            */
/* ------------------------------------------------------------------ */

/* The framebuffer is heap-allocated on purpose. Under AddressSanitizer the
 * redzones around it turn any stray write from a flush into a hard failure
 * instead of silent corruption of an adjacent global. */
typedef struct {
  gfx_color_t *pixels;
  int w;
  int h;
  long flush_calls;      /* total flushes received */
  long rejected_flushes; /* flushes whose rect fell outside the framebuffer */
  long pixels_written;
} mrt_fb_t;

static MRT_UNUSED void mrt_fb_init(mrt_fb_t *fb, int w, int h) {
  fb->w = w;
  fb->h = h;
  fb->pixels = (gfx_color_t *)calloc((size_t)w * (size_t)h, sizeof(gfx_color_t));
  fb->flush_calls = 0;
  fb->rejected_flushes = 0;
  fb->pixels_written = 0;
}

static MRT_UNUSED void mrt_fb_free(mrt_fb_t *fb) {
  free(fb->pixels);
  fb->pixels = NULL;
}

static MRT_UNUSED void mrt_fb_fill(mrt_fb_t *fb, gfx_color_t c) {
  long i, n = (long)fb->w * (long)fb->h;
  for (i = 0; i < n; ++i)
    fb->pixels[i] = c;
}

static MRT_UNUSED gfx_color_t mrt_fb_get(const mrt_fb_t *fb, int x, int y) {
  if (x < 0 || y < 0 || x >= fb->w || y >= fb->h)
    return (gfx_color_t)0;
  return fb->pixels[(long)y * fb->w + x];
}

/* Count pixels in the framebuffer that differ from `bg`. Used by the unit
 * tests to assert that clipped draws touched exactly the area they should. */
static MRT_UNUSED long mrt_fb_count_not(const mrt_fb_t *fb, gfx_color_t bg) {
  long i, n = (long)fb->w * (long)fb->h, c = 0;
  for (i = 0; i < n; ++i)
    if (fb->pixels[i] != bg)
      ++c;
  return c;
}

/* The flush callback. It validates the rect the renderer handed us before
 * copying anything: a renderer bug that produces an out-of-range tile shows up
 * as a counted rejection rather than as memory corruption. */
static MRT_UNUSED void mrt_fb_flush(gfx_renderer_t *r, int x, int y, int w, int h,
                         const gfx_color_t *pixels, void *user) {
  mrt_fb_t *fb = (mrt_fb_t *)user;
  int row;

  ++fb->flush_calls;

  if (!pixels || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > fb->w ||
      y + h > fb->h) {
    ++fb->rejected_flushes;
    return;
  }

  for (row = 0; row < h; ++row) {
    memcpy(fb->pixels + (long)(y + row) * fb->w + x,
           pixels + (long)row * r->tile_stride,
           (size_t)w * sizeof(gfx_color_t));
  }
  fb->pixels_written += (long)w * (long)h;
}

/* ------------------------------------------------------------------ */
/* sprite construction helpers                                         */
/* ------------------------------------------------------------------ */

/* Build a solid sprite whose pixel value encodes its index, so a blit can be
 * checked for correct source/destination alignment rather than just "wrote
 * something". Transparent pixels use `key`. */
static MRT_UNUSED void mrt_make_pattern(gfx_color_t *dst, int w, int h, gfx_color_t key,
                             int transparent_border) {
  int x, y;
  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      int edge = (x < transparent_border || y < transparent_border ||
                  x >= w - transparent_border || y >= h - transparent_border);
      dst[y * w + x] =
          edge ? key : (gfx_color_t)(0x1000u + (unsigned)(y * w + x));
    }
  }
}

static MRT_UNUSED void mrt_sprite_raw(gfx_sprite_t *s, const gfx_color_t *px, int w, int h,
                           gfx_color_t key, uint8_t flags) {
  s->width = w;
  s->height = h;
  s->pixels = px;
  s->runs = NULL;
  s->run_count = 0;
  s->row_start = NULL;
  s->key = key;
  s->flags = flags;
}

#endif /* MR_TEST_SUPPORT_H */
