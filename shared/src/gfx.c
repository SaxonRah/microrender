#include "gfx.h"
#include <string.h>

static int gfx_max_int(int a, int b) { return a > b ? a : b; }
static int gfx_min_int(int a, int b) { return a < b ? a : b; }

GFX_INLINE void gfx_fast_fill16(gfx_color_t GFX_PTR *dst, int count,
                                gfx_color_t color) {
  if (!dst || count <= 0)
    return;
#if GFX_COLOR_FORMAT == GFX_COLOR_FORMAT_INDEX8 &&                             \
    !defined(GFX_NO_LIBC_PIXEL_OPS)
  memset((void *)dst, (int)((unsigned char)color), (size_t)count);
#else
  {
    int i = 0;
    for (; i + 15 < count; i += 16) {
      dst[i + 0] = color;
      dst[i + 1] = color;
      dst[i + 2] = color;
      dst[i + 3] = color;
      dst[i + 4] = color;
      dst[i + 5] = color;
      dst[i + 6] = color;
      dst[i + 7] = color;
      dst[i + 8] = color;
      dst[i + 9] = color;
      dst[i + 10] = color;
      dst[i + 11] = color;
      dst[i + 12] = color;
      dst[i + 13] = color;
      dst[i + 14] = color;
      dst[i + 15] = color;
    }
    for (; i < count; ++i)
      dst[i] = color;
  }
#endif
}

GFX_INLINE void gfx_fast_copy16(gfx_color_t GFX_PTR *dst,
                                const gfx_color_t GFX_PTR *src, int count) {
  if (!dst || !src || count <= 0)
    return;
#if !defined(GFX_NO_LIBC_PIXEL_OPS) && !defined(__WATCOMC__) &&          \
    !(defined(GFX_PLATFORM_RP2350) && GFX_PLATFORM_RP2350)
  memcpy((void *)dst, (const void *)src, (size_t)count * sizeof(gfx_color_t));
#else
  {
    int i = 0;
    for (; i + 15 < count; i += 16) {
      dst[i + 0] = src[i + 0];
      dst[i + 1] = src[i + 1];
      dst[i + 2] = src[i + 2];
      dst[i + 3] = src[i + 3];
      dst[i + 4] = src[i + 4];
      dst[i + 5] = src[i + 5];
      dst[i + 6] = src[i + 6];
      dst[i + 7] = src[i + 7];
      dst[i + 8] = src[i + 8];
      dst[i + 9] = src[i + 9];
      dst[i + 10] = src[i + 10];
      dst[i + 11] = src[i + 11];
      dst[i + 12] = src[i + 12];
      dst[i + 13] = src[i + 13];
      dst[i + 14] = src[i + 14];
      dst[i + 15] = src[i + 15];
    }
    for (; i < count; ++i)
      dst[i] = src[i];
  }
#endif
}

static int gfx_clip_rect_to_active_tile(gfx_renderer_t GFX_PTR *r,
                                        int GFX_PTR *x0, int GFX_PTR *y0,
                                        int GFX_PTR *x1, int GFX_PTR *y1) {
  int tx0, ty0, tx1, ty1;
  if (!r || !x0 || !y0 || !x1 || !y1)
    return 0;
  tx0 = r->tile_x;
  ty0 = r->tile_y;
  tx1 = r->tile_x + r->tile_w;
  ty1 = r->tile_y + r->tile_h;
  *x0 = gfx_max_int(*x0, 0);
  *y0 = gfx_max_int(*y0, 0);
  *x1 = gfx_min_int(*x1, r->width);
  *y1 = gfx_min_int(*y1, r->height);
  *x0 = gfx_max_int(*x0, r->clip_x0);
  *y0 = gfx_max_int(*y0, r->clip_y0);
  *x1 = gfx_min_int(*x1, r->clip_x1);
  *y1 = gfx_min_int(*y1, r->clip_y1);
  *x0 = gfx_max_int(*x0, tx0);
  *x1 = gfx_min_int(*x1, tx1);
  *y0 = gfx_max_int(*y0, ty0);
  *y1 = gfx_min_int(*y1, ty1);
  return (*x0 < *x1) && (*y0 < *y1);
}

static int gfx_floor_div(int a, int b) {
  int q, r;
  if (b <= 0)
    return 0;
  q = a / b;
  r = a % b;
  if (r < 0)
    --q;
  return q;
}

static int gfx_mod_positive(int v, int m) {
  int r;
  if (m <= 0)
    return 0;
  r = v % m;
  return r < 0 ? r + m : r;
}

void gfx_init(gfx_renderer_t GFX_PTR *r, int width, int height,
              gfx_color_t GFX_PTR *tile_buffer, int tile_h, gfx_flush_fn flush,
              void GFX_PTR *user) {
  if (!r)
    return;
  r->width = width;
  r->height = height;
  r->tile_x = 0;
  r->tile_y = 0;
  r->tile_w = width;
  r->tile_h = tile_h;
  r->tile_stride = width;
  r->tile = tile_buffer;
  r->tile_capacity = (long)width * (long)(tile_h > 0 ? tile_h : 0);
  r->flush = flush;
  r->flush_begin = 0;
  r->flush_wait = 0;
  r->user = user;
  gfx_reset_clip(r);
}

void gfx_set_tile_capacity(gfx_renderer_t GFX_PTR *r, long pixels) {
  if (!r)
    return;
  r->tile_capacity = pixels > 0 ? pixels : 0;
}

void gfx_set_async_flush(gfx_renderer_t GFX_PTR *r, gfx_flush_begin_fn begin_fn,
                         gfx_flush_wait_fn wait_fn) {
  if (!r)
    return;
  r->flush_begin = begin_fn;
  r->flush_wait = wait_fn;
}

void gfx_begin_tile_rect(gfx_renderer_t GFX_PTR *r, int tile_x, int tile_y,
                         int tile_w, int tile_h) {
  int x0, y0, x1, y1;
  if (!r)
    return;
  if (tile_w <= 0 || tile_h <= 0) {
    r->tile_x = r->tile_y = r->tile_w = r->tile_h = r->tile_stride = 0;
    gfx_reset_clip(r);
    return;
  }
  x0 = gfx_max_int(tile_x, 0);
  y0 = gfx_max_int(tile_y, 0);
  x1 = gfx_min_int(tile_x + tile_w, r->width);
  y1 = gfx_min_int(tile_y + tile_h, r->height);
  r->tile_x = x0;
  r->tile_y = y0;
  r->tile_w = (x1 > x0) ? (x1 - x0) : 0;
  r->tile_h = (y1 > y0) ? (y1 - y0) : 0;
  r->tile_stride = r->tile_w;
  /* A caller-supplied dirty rect must never ask for more pixels than the tile
     buffer actually holds. Clamp height rather than trusting the request. */
  if (r->tile_capacity > 0 && r->tile_w > 0) {
    long max_rows = r->tile_capacity / (long)r->tile_w;
    if ((long)r->tile_h > max_rows)
      r->tile_h = (int)max_rows;
  }
  gfx_reset_clip(r);
}

void gfx_begin_tile(gfx_renderer_t GFX_PTR *r, int tile_y, int tile_h) {
  if (!r)
    return;
  gfx_begin_tile_rect(r, 0, tile_y, r->width, tile_h);
}

void gfx_flush_tile(gfx_renderer_t GFX_PTR *r) {
  if (r && r->flush && r->tile_w > 0 && r->tile_h > 0)
    r->flush(r, r->tile_x, r->tile_y, r->tile_w, r->tile_h, r->tile, r->user);
}

void gfx_render_tiled_region_ex(
    gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
    void (*draw_scene)(gfx_renderer_t GFX_PTR *r, void GFX_PTR *scene_user),
    void GFX_PTR *scene_user, gfx_color_t clear_color, unsigned flags) {
  int x0, y0, x1, y1, ty, step_h;
  if (!r || !draw_scene || w <= 0 || h <= 0)
    return;
  x0 = gfx_max_int(x, 0);
  y0 = gfx_max_int(y, 0);
  x1 = gfx_min_int(x + w, r->width);
  y1 = gfx_min_int(y + h, r->height);
  if (x0 >= x1 || y0 >= y1)
    return;
  step_h = r->tile_h;
  if (step_h <= 0)
    step_h = GFX_DEFAULT_TILE_H;
  for (ty = y0; ty < y1; ty += step_h) {
    int this_h = gfx_min_int(step_h, y1 - ty);
    gfx_begin_tile_rect(r, x0, ty, x1 - x0, this_h);
    if ((flags & GFX_RENDER_CLEAR) != 0u)
      gfx_clear_tile(r, clear_color);
    draw_scene(r, scene_user);
    gfx_flush_tile(r);
  }
  gfx_begin_tile(r, 0, step_h);
}

void gfx_render_tiled_region(
    gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
    void (*draw_scene)(gfx_renderer_t GFX_PTR *r, void GFX_PTR *scene_user),
    void GFX_PTR *scene_user, gfx_color_t clear_color) {
  gfx_render_tiled_region_ex(r, x, y, w, h, draw_scene, scene_user, clear_color,
                             GFX_RENDER_CLEAR);
}

void gfx_render_tiled_region_no_clear(
    gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
    void (*draw_scene)(gfx_renderer_t GFX_PTR *r, void GFX_PTR *scene_user),
    void GFX_PTR *scene_user) {
  gfx_render_tiled_region_ex(r, x, y, w, h, draw_scene, scene_user, 0, 0u);
}

void gfx_render_tiled_ex(gfx_renderer_t GFX_PTR *r,
                         void (*draw_scene)(gfx_renderer_t GFX_PTR *r,
                                            void GFX_PTR *scene_user),
                         void GFX_PTR *scene_user, gfx_color_t clear_color,
                         unsigned flags) {
  if (!r)
    return;
  gfx_render_tiled_region_ex(r, 0, 0, r->width, r->height, draw_scene,
                             scene_user, clear_color, flags);
}

void gfx_render_tiled(gfx_renderer_t GFX_PTR *r,
                      void (*draw_scene)(gfx_renderer_t GFX_PTR *r,
                                         void GFX_PTR *scene_user),
                      void GFX_PTR *scene_user, gfx_color_t clear_color) {
  gfx_render_tiled_ex(r, draw_scene, scene_user, clear_color, GFX_RENDER_CLEAR);
}

void gfx_render_tiled_no_clear(gfx_renderer_t GFX_PTR *r,
                               void (*draw_scene)(gfx_renderer_t GFX_PTR *r,
                                                  void GFX_PTR *scene_user),
                               void GFX_PTR *scene_user) {
  gfx_render_tiled_ex(r, draw_scene, scene_user, 0, 0u);
}

void gfx_render_tiled_pipelined(
    gfx_renderer_t GFX_PTR *r, gfx_color_t GFX_PTR *second_tile_buffer,
    void (*draw_scene)(gfx_renderer_t GFX_PTR *r, void GFX_PTR *scene_user),
    void GFX_PTR *scene_user, gfx_color_t clear_color, unsigned flags) {
  gfx_color_t GFX_PTR *first_tile;
  gfx_color_t GFX_PTR *buffers[2];
  int y, th, active, pending;
  if (!r || !r->flush_begin || !r->flush_wait || !second_tile_buffer) {
    gfx_render_tiled_ex(r, draw_scene, scene_user, clear_color, flags);
    return;
  }
  first_tile = r->tile;
  buffers[0] = first_tile;
  buffers[1] = second_tile_buffer;
  th = r->tile_h;
  if (th <= 0)
    th = GFX_DEFAULT_TILE_H;
  active = 0;
  pending = 0;
  for (y = 0; y < r->height; y += th) {
    r->tile = buffers[active];
    gfx_begin_tile(r, y, th);
    if ((flags & GFX_RENDER_CLEAR) != 0u)
      gfx_clear_tile(r, clear_color);
    if (draw_scene)
      draw_scene(r, scene_user);
    if (pending) {
      r->flush_wait(r, r->user);
      pending = 0;
    }
    if (r->tile_h > 0) {
      r->flush_begin(r, r->tile_x, r->tile_y, r->tile_w, r->tile_h, r->tile,
                     r->user);
      pending = 1;
    }
    active ^= 1;
  }
  if (pending)
    r->flush_wait(r, r->user);
  r->tile = first_tile;
  gfx_begin_tile(r, 0, th);
}

void gfx_set_clip(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h) {
  if (!r)
    return;
  r->clip_x0 = x;
  r->clip_y0 = y;
  r->clip_x1 = x + w;
  r->clip_y1 = y + h;
}

void gfx_reset_clip(gfx_renderer_t GFX_PTR *r) {
  if (!r)
    return;
  r->clip_x0 = 0;
  r->clip_y0 = 0;
  r->clip_x1 = r->width;
  r->clip_y1 = r->height;
}

void gfx_clear_tile(gfx_renderer_t GFX_PTR *r, gfx_color_t color) {
  int y;
  gfx_color_t GFX_PTR *dst;
  if (!r || r->tile_w <= 0 || r->tile_h <= 0 || !r->tile)
    return;
  if (r->tile_stride == r->tile_w) {
    gfx_fast_fill16(r->tile, r->tile_w * r->tile_h, color);
    return;
  }
  dst = r->tile;
  for (y = 0; y < r->tile_h; ++y) {
    gfx_fast_fill16(dst, r->tile_w, color);
    dst += r->tile_stride;
  }
}

void gfx_draw_pixel(gfx_renderer_t GFX_PTR *r, int x, int y,
                    gfx_color_t color) {
  if (!r || !r->tile)
    return;
  if (x < r->clip_x0 || x >= r->clip_x1 || y < r->clip_y0 || y >= r->clip_y1)
    return;
  if (x < r->tile_x || x >= r->tile_x + r->tile_w || y < r->tile_y ||
      y >= r->tile_y + r->tile_h)
    return;
  if (x < 0 || x >= r->width || y < 0 || y >= r->height)
    return;
  r->tile[(y - r->tile_y) * r->tile_stride + (x - r->tile_x)] = color;
}

void gfx_draw_hline(gfx_renderer_t GFX_PTR *r, int x, int y, int w,
                    gfx_color_t color) {
  int x0 = x, y0 = y, x1 = x + w, y1 = y + 1;
  gfx_color_t GFX_PTR *dst;
  if (!r || w <= 0)
    return;
  if (!gfx_clip_rect_to_active_tile(r, &x0, &y0, &x1, &y1))
    return;
  dst = r->tile + (y0 - r->tile_y) * r->tile_stride + (x0 - r->tile_x);
  gfx_fast_fill16(dst, x1 - x0, color);
}

void gfx_draw_vline(gfx_renderer_t GFX_PTR *r, int x, int y, int h,
                    gfx_color_t color) {
  int x0 = x, y0 = y, x1 = x + 1, y1 = y + h, yy;
  gfx_color_t GFX_PTR *dst;
  if (!r || h <= 0)
    return;
  if (!gfx_clip_rect_to_active_tile(r, &x0, &y0, &x1, &y1))
    return;
  dst = r->tile + (y0 - r->tile_y) * r->tile_stride + (x0 - r->tile_x);
  for (yy = y0; yy < y1; ++yy) {
    *dst = color;
    dst += r->tile_stride;
  }
}

void gfx_fill_rect(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                   gfx_color_t color) {
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h, yy, row_count;
  gfx_color_t GFX_PTR *dst;
  if (!r || w <= 0 || h <= 0)
    return;
  if (!gfx_clip_rect_to_active_tile(r, &x0, &y0, &x1, &y1))
    return;
  row_count = x1 - x0;
  dst = r->tile + (y0 - r->tile_y) * r->tile_stride + (x0 - r->tile_x);
  for (yy = y0; yy < y1; ++yy) {
    gfx_fast_fill16(dst, row_count, color);
    dst += r->tile_stride;
  }
}

void gfx_draw_rect(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                   gfx_color_t color) {
  if (w <= 0 || h <= 0)
    return;
  gfx_draw_hline(r, x, y, w, color);
  gfx_draw_hline(r, x, y + h - 1, w, color);
  gfx_draw_vline(r, x, y, h, color);
  gfx_draw_vline(r, x + w - 1, y, h, color);
}

void gfx_draw_line(gfx_renderer_t GFX_PTR *r, int x0, int y0, int x1, int y1,
                   gfx_color_t color) {
  int dx, sx, dy, sy, err;
  if (x0 == x1) {
    int top = y0 < y1 ? y0 : y1;
    gfx_draw_vline(r, x0, top, (y0 < y1 ? y1 - y0 : y0 - y1) + 1, color);
    return;
  }
  if (y0 == y1) {
    int left = x0 < x1 ? x0 : x1;
    gfx_draw_hline(r, left, y0, (x0 < x1 ? x1 - x0 : x0 - x1) + 1, color);
    return;
  }
  dx = x0 < x1 ? (x1 - x0) : (x0 - x1);
  sx = x0 < x1 ? 1 : -1;
  dy = y0 < y1 ? (y0 - y1) : (y1 - y0);
  sy = y0 < y1 ? 1 : -1;
  err = dx + dy;
  for (;;) {
    int e2;
    gfx_draw_pixel(r, x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    /* err is routinely negative here; `err << 1` is undefined behaviour in C99
       for negative operands (UBSan flags it on the first frame). Multiplying by
       two is well defined and compiles to the same shift. */
    e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static int gfx_sprite_ready(const gfx_sprite_t GFX_PTR *s) {
  if (!s || s->width <= 0 || s->height <= 0)
    return 0;
#if defined(GFX_INT_IS_16BIT) && GFX_INT_IS_16BIT
  /* Row addressing below is `row * s->width` in int math. On a 16-bit int
     target that wraps above 32767 pixels and corrupts memory silently, so
     refuse to draw instead. 32-bit targets compile this away entirely. */
  if ((long)s->width * (long)s->height > GFX_MAX_SPRITE_PIXELS)
    return 0;
#endif
  if ((s->flags & GFX_SPRITE_RLE) != 0) {
    if (!s->pixels || !s->runs || s->run_count <= 0)
      return 0;
    if ((s->flags & GFX_SPRITE_RLE_ROWSTART) != 0 && !s->row_start)
      return 0;
    return 1;
  }
  return s->pixels != 0;
}

static int gfx_sprite_inside_active(const gfx_renderer_t GFX_PTR *r,
                                    const gfx_sprite_t GFX_PTR *s, int x,
                                    int y) {
  int x1, y1;
  if (!r || !s)
    return 0;
  x1 = x + s->width;
  y1 = y + s->height;
  return x >= r->clip_x0 && y >= r->clip_y0 && x1 <= r->clip_x1 &&
         y1 <= r->clip_y1 && x >= r->tile_x && y >= r->tile_y &&
         x1 <= r->tile_x + r->tile_w && y1 <= r->tile_y + r->tile_h && x >= 0 &&
         y >= 0 && x1 <= r->width && y1 <= r->height;
}

static void gfx_blit_sprite_raw_unchecked(gfx_renderer_t GFX_PTR *r,
                                          const gfx_sprite_t GFX_PTR *s, int x,
                                          int y) {
  int row;
  const gfx_color_t GFX_PTR *src = s->pixels;
  gfx_color_t GFX_PTR *dst =
      r->tile + (y - r->tile_y) * r->tile_stride + (x - r->tile_x);
  for (row = 0; row < s->height; ++row) {
    gfx_fast_copy16(dst, src, s->width);
    src += s->width;
    dst += r->tile_stride;
  }
}

static void gfx_blit_sprite_raw_clipped_unchecked(gfx_renderer_t GFX_PTR *r,
                                                  const gfx_sprite_t GFX_PTR *s,
                                                  int x, int y) {
  int ax0 = x, ay0 = y, ax1 = x + s->width, ay1 = y + s->height;
  int sx0, sy0, row, w;
  if (!gfx_clip_rect_to_active_tile(r, &ax0, &ay0, &ax1, &ay1))
    return;
  sx0 = ax0 - x;
  sy0 = ay0 - y;
  w = ax1 - ax0;
  for (row = 0; row < ay1 - ay0; ++row) {
    const gfx_color_t GFX_PTR *src = s->pixels + (sy0 + row) * s->width + sx0;
    gfx_color_t GFX_PTR *dst =
        r->tile + (ay0 + row - r->tile_y) * r->tile_stride + (ax0 - r->tile_x);
    gfx_fast_copy16(dst, src, w);
  }
}

static void gfx_blit_sprite_colorkey_unchecked(gfx_renderer_t GFX_PTR *r,
                                               const gfx_sprite_t GFX_PTR *s,
                                               int x, int y) {
  int row, col;
  gfx_color_t key = s->key;
  const gfx_color_t GFX_PTR *src = s->pixels;
  gfx_color_t GFX_PTR *dst =
      r->tile + (y - r->tile_y) * r->tile_stride + (x - r->tile_x);
  for (row = 0; row < s->height; ++row) {
    for (col = 0; col < s->width; ++col) {
      gfx_color_t p = src[col];
      if (p != key)
        dst[col] = p;
    }
    src += s->width;
    dst += r->tile_stride;
  }
}

static void gfx_blit_sprite_colorkey_clipped_unchecked(
    gfx_renderer_t GFX_PTR *r, const gfx_sprite_t GFX_PTR *s, int x, int y) {
  int ax0 = x, ay0 = y, ax1 = x + s->width, ay1 = y + s->height;
  int sx0, sy0, row, col, w;
  gfx_color_t key = s->key;
  if (!gfx_clip_rect_to_active_tile(r, &ax0, &ay0, &ax1, &ay1))
    return;
  sx0 = ax0 - x;
  sy0 = ay0 - y;
  w = ax1 - ax0;
  for (row = 0; row < ay1 - ay0; ++row) {
    const gfx_color_t GFX_PTR *src = s->pixels + (sy0 + row) * s->width + sx0;
    gfx_color_t GFX_PTR *dst =
        r->tile + (ay0 + row - r->tile_y) * r->tile_stride + (ax0 - r->tile_x);
    for (col = 0; col < w; ++col) {
      gfx_color_t p = src[col];
      if (p != key)
        dst[col] = p;
    }
  }
}

static int gfx_sprite_has_row_index(const gfx_sprite_t GFX_PTR *s) {
  return s && (s->flags & GFX_SPRITE_RLE) != 0 &&
         (s->flags & GFX_SPRITE_RLE_ROWSTART) != 0 && s->row_start != 0;
}

static void gfx_blit_sprite_rle_rowstart_fast(
    gfx_renderer_t GFX_PTR *r, const gfx_sprite_t GFX_PTR *s, int x, int y,
    int clipped) {
  int sy0;
  int sy1;
  int sy;
  int ax0;
  int ax1;

  if (!r || !s || !r->tile || !s->pixels || !s->runs || !s->row_start)
    return;

  if (!clipped) {
    for (sy = 0; sy < s->height; ++sy) {
      int first = (int)s->row_start[sy];
      int last = (int)s->row_start[sy + 1];
      gfx_color_t GFX_PTR *dst_row =
          r->tile + (y + sy - r->tile_y) * r->tile_stride + (x - r->tile_x);
      int i;
      if (last > s->run_count)
        last = s->run_count;
      if (first > last)
        first = last;
      for (i = first; i < last; ++i) {
        const gfx_rle_run_t GFX_PTR *run = s->runs + i;
        gfx_fast_copy16(dst_row + run->x, s->pixels + run->offset, run->len);
      }
    }
    return;
  }

  ax0 = r->clip_x0;
  if (ax0 < r->tile_x)
    ax0 = r->tile_x;
  if (ax0 < 0)
    ax0 = 0;
  ax1 = r->clip_x1;
  if (ax1 > r->tile_x + r->tile_w)
    ax1 = r->tile_x + r->tile_w;
  if (ax1 > r->width)
    ax1 = r->width;
  if (ax0 >= ax1)
    return;

  sy0 = r->clip_y0;
  if (sy0 < r->tile_y)
    sy0 = r->tile_y;
  if (sy0 < 0)
    sy0 = 0;
  sy1 = r->clip_y1;
  if (sy1 > r->tile_y + r->tile_h)
    sy1 = r->tile_y + r->tile_h;
  if (sy1 > r->height)
    sy1 = r->height;

  sy0 -= y;
  sy1 -= y;
  if (sy0 < 0)
    sy0 = 0;
  if (sy1 > s->height)
    sy1 = s->height;
  if (sy0 >= sy1)
    return;

  for (sy = sy0; sy < sy1; ++sy) {
    int first = (int)s->row_start[sy];
    int last = (int)s->row_start[sy + 1];
    gfx_color_t GFX_PTR *dst_row =
        r->tile + (y + sy - r->tile_y) * r->tile_stride - r->tile_x;
    int i;
    if (last > s->run_count)
      last = s->run_count;
    if (first > last)
      first = last;
    for (i = first; i < last; ++i) {
      const gfx_rle_run_t GFX_PTR *run = s->runs + i;
      int dx = x + (int)run->x;
      int len = (int)run->len;
      int sx = 0;
      int dx1;
      if (len <= 0)
        continue;
      dx1 = dx + len;
      if (dx < ax0) {
        sx = ax0 - dx;
        dx = ax0;
      }
      if (dx1 > ax1)
        dx1 = ax1;
      len = dx1 - dx;
      if (len > 0)
        gfx_fast_copy16(dst_row + dx, s->pixels + run->offset + sx, len);
    }
  }
}

static void gfx_blit_rle_run(gfx_renderer_t GFX_PTR *r,
                             const gfx_sprite_t GFX_PTR *s,
                             const gfx_rle_run_t GFX_PTR *run, int x, int y,
                             int clipped, gfx_blit_stats_t GFX_PTR *stats) {
  int dx = x + run->x;
  int dy = y + run->y;
  int sx0 = 0;
  int len = run->len;
  if (len <= 0)
    return;
  if (clipped) {
    int dx1 = dx + len;
    if (dy < r->clip_y0 || dy >= r->clip_y1 || dy < r->tile_y ||
        dy >= r->tile_y + r->tile_h || dy < 0 || dy >= r->height)
      return;
    if (dx < r->clip_x0) {
      sx0 += r->clip_x0 - dx;
      dx = r->clip_x0;
    }
    if (dx < r->tile_x) {
      sx0 += r->tile_x - dx;
      dx = r->tile_x;
    }
    if (dx < 0) {
      sx0 += -dx;
      dx = 0;
    }
    if (dx1 > r->clip_x1)
      dx1 = r->clip_x1;
    if (dx1 > r->tile_x + r->tile_w)
      dx1 = r->tile_x + r->tile_w;
    if (dx1 > r->width)
      dx1 = r->width;
    len = dx1 - dx;
    if (len <= 0)
      return;
  }
  gfx_fast_copy16(r->tile + (dy - r->tile_y) * r->tile_stride +
                      (dx - r->tile_x),
                  s->pixels + run->offset + sx0, len);
  if (stats) {
    stats->rle_runs_drawn++;
    stats->rle_pixels_copied += (unsigned long)len;
    stats->sprite_pixels_drawn += (unsigned long)len;
  }
}

static void gfx_blit_sprite_rle_unchecked(gfx_renderer_t GFX_PTR *r,
                                          const gfx_sprite_t GFX_PTR *s, int x,
                                          int y, int clipped,
                                          gfx_blit_stats_t GFX_PTR *stats) {
  int i;
  if (!stats && gfx_sprite_has_row_index(s)) {
    gfx_blit_sprite_rle_rowstart_fast(r, s, x, y, clipped);
    return;
  }
  if (gfx_sprite_has_row_index(s)) {
    int sy0 = 0;
    int sy1 = s->height;
    if (clipped) {
      int y0 = gfx_max_int(gfx_max_int(r->clip_y0, r->tile_y), 0);
      int y1 = gfx_min_int(gfx_min_int(r->clip_y1, r->tile_y + r->tile_h),
                           r->height);
      if (y + sy0 < y0)
        sy0 = y0 - y;
      if (y + sy1 > y1)
        sy1 = y1 - y;
      if (sy0 < 0)
        sy0 = 0;
      if (sy1 > s->height)
        sy1 = s->height;
      if (sy0 >= sy1)
        return;
    }
    for (; sy0 < sy1; ++sy0) {
      int first = (int)s->row_start[sy0];
      int last = (int)s->row_start[sy0 + 1];
      if (first < 0)
        first = 0;
      if (last > s->run_count)
        last = s->run_count;
      for (i = first; i < last; ++i)
        gfx_blit_rle_run(r, s, s->runs + i, x, y, clipped, stats);
    }
    return;
  }
  for (i = 0; i < s->run_count; ++i)
    gfx_blit_rle_run(r, s, s->runs + i, x, y, clipped, stats);
}

void gfx_blit_sprite_unclipped(gfx_renderer_t GFX_PTR *r,
                               const gfx_sprite_t GFX_PTR *s, int x, int y) {
  if (!r || !gfx_sprite_ready(s))
    return;
  if ((s->flags & GFX_SPRITE_RLE) != 0) {
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 0, 0);
  } else {
    gfx_blit_sprite_raw_unchecked(r, s, x, y);
  }
}

void gfx_blit_sprite_clipped(gfx_renderer_t GFX_PTR *r,
                             const gfx_sprite_t GFX_PTR *s, int x, int y) {
  if (!r || !gfx_sprite_ready(s))
    return;
  if ((s->flags & GFX_SPRITE_RLE) != 0) {
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 1, 0);
  } else {
    gfx_blit_sprite_raw_clipped_unchecked(r, s, x, y);
  }
}

void gfx_blit_sprite_colorkey_unclipped(gfx_renderer_t GFX_PTR *r,
                                        const gfx_sprite_t GFX_PTR *s, int x,
                                        int y) {
  if (!r || !gfx_sprite_ready(s))
    return;
  if ((s->flags & GFX_SPRITE_RLE) != 0)
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 0, 0);
  else
    gfx_blit_sprite_colorkey_unchecked(r, s, x, y);
}

void gfx_blit_sprite_colorkey_clipped(gfx_renderer_t GFX_PTR *r,
                                      const gfx_sprite_t GFX_PTR *s, int x,
                                      int y) {
  if (!r || !gfx_sprite_ready(s))
    return;
  if ((s->flags & GFX_SPRITE_RLE) != 0)
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 1, 0);
  else
    gfx_blit_sprite_colorkey_clipped_unchecked(r, s, x, y);
}

void gfx_blit_sprite_rle_unclipped(gfx_renderer_t GFX_PTR *r,
                                   const gfx_sprite_t GFX_PTR *s, int x,
                                   int y) {
  if (r && gfx_sprite_ready(s))
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 0, 0);
}

void gfx_blit_sprite_rle_clipped(gfx_renderer_t GFX_PTR *r,
                                 const gfx_sprite_t GFX_PTR *s, int x, int y) {
  if (r && gfx_sprite_ready(s))
    gfx_blit_sprite_rle_unchecked(r, s, x, y, 1, 0);
}

void gfx_blit(gfx_renderer_t GFX_PTR *r, const gfx_sprite_t GFX_PTR *s, int x,
              int y) {
  int inside;
  if (!r || !gfx_sprite_ready(s))
    return;
  inside = gfx_sprite_inside_active(r, s, x, y);
  if ((s->flags & GFX_SPRITE_RLE) != 0) {
    gfx_blit_sprite_rle_unchecked(r, s, x, y, !inside, 0);
  } else if ((s->flags & GFX_SPRITE_COLORKEY) != 0) {
    if (inside)
      gfx_blit_sprite_colorkey_unchecked(r, s, x, y);
    else
      gfx_blit_sprite_colorkey_clipped_unchecked(r, s, x, y);
  } else {
    if (inside)
      gfx_blit_sprite_raw_unchecked(r, s, x, y);
    else
      gfx_blit_sprite_raw_clipped_unchecked(r, s, x, y);
  }
}

void gfx_blit_counted(gfx_renderer_t GFX_PTR *r, const gfx_sprite_t GFX_PTR *s,
                      int x, int y, gfx_blit_stats_t GFX_PTR *stats) {
  int inside;
  if (stats)
    stats->sprites_considered++;
  if (!r || !gfx_sprite_ready(s)) {
    if (stats)
      stats->sprites_rejected++;
    return;
  }
  if (!gfx_rects_overlap(
          gfx_sprite_rect(s, x, y),
          gfx_rect_make(r->tile_x, r->tile_y, r->tile_w, r->tile_h))) {
    if (stats)
      stats->sprites_rejected++;
    return;
  }
  inside = gfx_sprite_inside_active(r, s, x, y);
  if ((s->flags & GFX_SPRITE_RLE) != 0) {
    gfx_blit_sprite_rle_unchecked(r, s, x, y, !inside, stats);
  } else {
    gfx_blit(r, s, x, y);
  }
  if (stats)
    stats->sprites_drawn++;
}

int gfx_rle_build_from_colorkey_indexed(
    const gfx_color_t GFX_PTR *src, int width, int height, gfx_color_t key,
    gfx_rle_run_t GFX_PTR *runs, int max_runs, gfx_color_t GFX_PTR *pixel_pool,
    int max_pixels, uint16_t GFX_PTR *row_start, int max_row_start,
    gfx_sprite_t GFX_PTR *out_sprite) {
  int y, run_count = 0, pixel_count = 0;
  if (!src || !runs || !pixel_pool || !out_sprite || width <= 0 || height <= 0)
    return 0;
  if (row_start && max_row_start < height + 1)
    return 0;
  for (y = 0; y < height; ++y) {
    int x = 0;
    if (row_start)
      row_start[y] = (uint16_t)run_count;
    while (x < width) {
      const gfx_color_t GFX_PTR *row = src + y * width;
      int start, len, i;
      while (x < width && row[x] == key)
        ++x;
      if (x >= width)
        break;
      start = x;
      while (x < width && row[x] != key)
        ++x;
      len = x - start;
      if (run_count >= max_runs || pixel_count + len > max_pixels)
        return 0;
      runs[run_count].x = (int16_t)start;
      runs[run_count].y = (int16_t)y;
      runs[run_count].len = (int16_t)len;
      runs[run_count].offset = (uint32_t)pixel_count;
      for (i = 0; i < len; ++i)
        pixel_pool[pixel_count + i] = row[start + i];
      pixel_count += len;
      ++run_count;
    }
  }
  if (row_start)
    row_start[height] = (uint16_t)run_count;
  out_sprite->width = width;
  out_sprite->height = height;
  out_sprite->pixels = pixel_pool;
  out_sprite->runs = runs;
  out_sprite->run_count = run_count;
  out_sprite->row_start = row_start;
  out_sprite->key = key;
  out_sprite->flags =
      (uint8_t)(GFX_SPRITE_RLE | (row_start ? GFX_SPRITE_RLE_ROWSTART : 0u));
  return 1;
}

int gfx_rle_build_from_colorkey(const gfx_color_t GFX_PTR *src, int width,
                                int height, gfx_color_t key,
                                gfx_rle_run_t GFX_PTR *runs, int max_runs,
                                gfx_color_t GFX_PTR *pixel_pool, int max_pixels,
                                gfx_sprite_t GFX_PTR *out_sprite) {
  return gfx_rle_build_from_colorkey_indexed(src, width, height, key, runs,
                                             max_runs, pixel_pool, max_pixels,
                                             0, 0, out_sprite);
}

int gfx_sprite_rle_validate(const gfx_sprite_t GFX_PTR *s,
                            unsigned long pool_pixels) {
  int i;
  if (!s || (s->flags & GFX_SPRITE_RLE) == 0)
    return 0;
  if (!s->pixels || !s->runs || s->run_count <= 0)
    return 0;
  if (s->width <= 0 || s->height <= 0)
    return 0;
  if ((long)s->width * (long)s->height > GFX_MAX_SPRITE_PIXELS)
    return 0;

  for (i = 0; i < s->run_count; ++i) {
    const gfx_rle_run_t GFX_PTR *run = s->runs + i;
    long end;
    if (run->len <= 0)
      return 0;
    if (run->x < 0 || run->y < 0)
      return 0;
    if (run->y >= s->height)
      return 0;
    if ((long)run->x + (long)run->len > (long)s->width)
      return 0;
    end = (long)run->offset + (long)run->len;
    if (end > (long)pool_pixels)
      return 0;
  }

  if ((s->flags & GFX_SPRITE_RLE_ROWSTART) != 0) {
    int row;
    if (!s->row_start)
      return 0;
    /* row_start must be non-decreasing, end at run_count, and every run it
       points at must actually belong to that row. The fast blit path indexes
       runs purely through this table, so a lie here is a memory-safety bug. */
    for (row = 0; row < s->height; ++row) {
      int first = (int)s->row_start[row];
      int last = (int)s->row_start[row + 1];
      int j;
      if (first > last || last > s->run_count)
        return 0;
      for (j = first; j < last; ++j) {
        if (s->runs[j].y != (int16_t)row)
          return 0;
      }
    }
    if ((int)s->row_start[0] != 0)
      return 0;
    if ((int)s->row_start[s->height] != s->run_count)
      return 0;
  }

  return 1;
}

static void gfx_blit_tile_fast(gfx_renderer_t GFX_PTR *r,
                               const gfx_sprite_t GFX_PTR *tile, int x, int y) {
  int ax0;
  int ay0;
  int ax1;
  int ay1;
  int sx0;
  int sy0;
  int row;
  int w;
  const gfx_color_t GFX_PTR *src;
  gfx_color_t GFX_PTR *dst;

  if (!r || !tile || !tile->pixels || !r->tile)
    return;

  /* Tilemaps are overwhelmingly raw, opaque, fixed-size sprites.  Going
   * through gfx_blit() for every 16x16 map cell costs readiness checks,
   * flag dispatch, an inside test, and then another clipped/unclipped helper
   * call.  This fused path does one clipped raw copy directly. */
  if ((tile->flags & (GFX_SPRITE_RLE | GFX_SPRITE_COLORKEY)) != 0u) {
    gfx_blit(r, tile, x, y);
    return;
  }

  ax0 = x;
  ay0 = y;
  ax1 = x + tile->width;
  ay1 = y + tile->height;

  if (ax0 < r->clip_x0)
    ax0 = r->clip_x0;
  if (ay0 < r->clip_y0)
    ay0 = r->clip_y0;
  if (ax1 > r->clip_x1)
    ax1 = r->clip_x1;
  if (ay1 > r->clip_y1)
    ay1 = r->clip_y1;
  if (ax0 < r->tile_x)
    ax0 = r->tile_x;
  if (ay0 < r->tile_y)
    ay0 = r->tile_y;
  if (ax1 > r->tile_x + r->tile_w)
    ax1 = r->tile_x + r->tile_w;
  if (ay1 > r->tile_y + r->tile_h)
    ay1 = r->tile_y + r->tile_h;
  if (ax0 < 0)
    ax0 = 0;
  if (ay0 < 0)
    ay0 = 0;
  if (ax1 > r->width)
    ax1 = r->width;
  if (ay1 > r->height)
    ay1 = r->height;
  if (ax0 >= ax1 || ay0 >= ay1)
    return;

  sx0 = ax0 - x;
  sy0 = ay0 - y;
  w = ax1 - ax0;
  src = tile->pixels + sy0 * tile->width + sx0;
  dst = r->tile + (ay0 - r->tile_y) * r->tile_stride + (ax0 - r->tile_x);

  for (row = ay0; row < ay1; ++row) {
    gfx_fast_copy16(dst, src, w);
    src += tile->width;
    dst += r->tile_stride;
  }
}

void gfx_draw_tilemap(gfx_renderer_t GFX_PTR *r,
                      const gfx_tilemap_t GFX_PTR *tm, int camera_x,
                      int camera_y, int dst_x, int dst_y, int view_w,
                      int view_h) {
  int tw, th, start_col, start_row, off_x, off_y, cols, rows, ty;
  int old_x0, old_y0, old_x1, old_y1;
  int active_x0, active_y0, active_x1, active_y1;
  int first_tx, last_tx, first_ty, last_ty;
  if (!r || !tm || !tm->tiles || !tm->tileset || tm->map_w <= 0 ||
      tm->map_h <= 0 || tm->tile_w <= 0 || tm->tile_h <= 0 ||
      tm->tileset_count <= 0 || view_w <= 0 || view_h <= 0)
    return;
  tw = tm->tile_w;
  th = tm->tile_h;
  start_col = gfx_floor_div(camera_x, tw);
  start_row = gfx_floor_div(camera_y, th);
  off_x = camera_x - start_col * tw;
  off_y = camera_y - start_row * th;
  cols = view_w / tw + 2;
  rows = view_h / th + 2;
  old_x0 = r->clip_x0;
  old_y0 = r->clip_y0;
  old_x1 = r->clip_x1;
  old_y1 = r->clip_y1;
  gfx_set_clip(r, dst_x, dst_y, view_w, view_h);

  active_x0 = dst_x;
  if (active_x0 < r->tile_x)
    active_x0 = r->tile_x;
  if (active_x0 < r->clip_x0)
    active_x0 = r->clip_x0;
  if (active_x0 < 0)
    active_x0 = 0;
  active_y0 = dst_y;
  if (active_y0 < r->tile_y)
    active_y0 = r->tile_y;
  if (active_y0 < r->clip_y0)
    active_y0 = r->clip_y0;
  if (active_y0 < 0)
    active_y0 = 0;
  active_x1 = dst_x + view_w;
  if (active_x1 > r->tile_x + r->tile_w)
    active_x1 = r->tile_x + r->tile_w;
  if (active_x1 > r->clip_x1)
    active_x1 = r->clip_x1;
  if (active_x1 > r->width)
    active_x1 = r->width;
  active_y1 = dst_y + view_h;
  if (active_y1 > r->tile_y + r->tile_h)
    active_y1 = r->tile_y + r->tile_h;
  if (active_y1 > r->clip_y1)
    active_y1 = r->clip_y1;
  if (active_y1 > r->height)
    active_y1 = r->height;

  if (active_x0 >= active_x1 || active_y0 >= active_y1) {
    gfx_set_clip(r, old_x0, old_y0, old_x1 - old_x0, old_y1 - old_y0);
    return;
  }

  first_tx = gfx_floor_div(active_x0 - dst_x + off_x, tw);
  last_tx = gfx_floor_div(active_x1 - dst_x + off_x + tw - 1, tw);
  first_ty = gfx_floor_div(active_y0 - dst_y + off_y, th);
  last_ty = gfx_floor_div(active_y1 - dst_y + off_y + th - 1, th);

  if (first_tx < 0)
    first_tx = 0;
  if (first_ty < 0)
    first_ty = 0;
  if (last_tx > cols)
    last_tx = cols;
  if (last_ty > rows)
    last_ty = rows;

  for (ty = first_ty; ty < last_ty; ++ty) {
    int tx;
    int map_y = gfx_mod_positive(start_row + ty, tm->map_h);
    int map_x = gfx_mod_positive(start_col + first_tx, tm->map_w);
    int screen_y = dst_y + ty * th - off_y;
    for (tx = first_tx; tx < last_tx; ++tx) {
      int screen_x = dst_x + tx * tw - off_x;
      unsigned tile_id = tm->tiles[map_y * tm->map_w + map_x];
      if (tile_id < (unsigned)tm->tileset_count)
        gfx_blit_tile_fast(r, &tm->tileset[tile_id], screen_x, screen_y);
      ++map_x;
      if (map_x == tm->map_w)
        map_x = 0;
    }
  }
  gfx_set_clip(r, old_x0, old_y0, old_x1 - old_x0, old_y1 - old_y0);
}

gfx_rect_t gfx_rect_make(int x, int y, int w, int h) {
  gfx_rect_t r;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  return r;
}

gfx_rect_t gfx_rect_clip(gfx_rect_t r, int screen_w, int screen_h) {
  int x0 = gfx_max_int(r.x, 0);
  int y0 = gfx_max_int(r.y, 0);
  int x1 = gfx_min_int(r.x + r.w, screen_w);
  int y1 = gfx_min_int(r.y + r.h, screen_h);
  if (x1 < x0)
    x1 = x0;
  if (y1 < y0)
    y1 = y0;
  return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

gfx_rect_t gfx_rect_union(gfx_rect_t a, gfx_rect_t b) {
  int x0, y0, x1, y1;
  if (gfx_rect_empty(a))
    return b;
  if (gfx_rect_empty(b))
    return a;
  x0 = gfx_min_int(a.x, b.x);
  y0 = gfx_min_int(a.y, b.y);
  x1 = gfx_max_int(a.x + a.w, b.x + b.w);
  y1 = gfx_max_int(a.y + a.h, b.y + b.h);
  return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

gfx_rect_t gfx_rect_intersection(gfx_rect_t a, gfx_rect_t b) {
  int x0 = gfx_max_int(a.x, b.x);
  int y0 = gfx_max_int(a.y, b.y);
  int x1 = gfx_min_int(a.x + a.w, b.x + b.w);
  int y1 = gfx_min_int(a.y + a.h, b.y + b.h);
  if (x1 <= x0 || y1 <= y0)
    return gfx_rect_make(0, 0, 0, 0);
  return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

int gfx_rect_empty(gfx_rect_t r) { return r.w <= 0 || r.h <= 0; }
long gfx_rect_area(gfx_rect_t r) {
  return gfx_rect_empty(r) ? 0L : (long)r.w * (long)r.h;
}

int gfx_rects_overlap(gfx_rect_t a, gfx_rect_t b) {
  return !gfx_rect_empty(a) && !gfx_rect_empty(b) && a.x < b.x + b.w &&
         a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

int gfx_rects_touch_or_overlap(gfx_rect_t a, gfx_rect_t b) {
  return !gfx_rect_empty(a) && !gfx_rect_empty(b) && a.x <= b.x + b.w &&
         a.x + a.w >= b.x && a.y <= b.y + b.h && a.y + a.h >= b.y;
}

gfx_rect_t gfx_sprite_rect(const gfx_sprite_t GFX_PTR *s, int x, int y) {
  if (!s)
    return gfx_rect_make(x, y, 0, 0);
  return gfx_rect_make(x, y, s->width, s->height);
}

void gfx_sprite_instance_init(gfx_sprite_instance_t GFX_PTR *inst,
                              const gfx_sprite_t GFX_PTR *sprite, int x, int y,
                              int z, int visible) {
  if (!inst)
    return;
  inst->sprite = sprite;
  inst->x = inst->old_x = x;
  inst->y = inst->old_y = y;
  inst->z = z;
  inst->visible = visible ? 1u : 0u;
}

void gfx_sprite_instance_set_pos(gfx_sprite_instance_t GFX_PTR *inst, int x,
                                 int y) {
  if (!inst)
    return;
  inst->old_x = inst->x;
  inst->old_y = inst->y;
  inst->x = x;
  inst->y = y;
}

gfx_rect_t gfx_sprite_instance_rect(const gfx_sprite_instance_t GFX_PTR *inst) {
  if (!inst)
    return gfx_rect_make(0, 0, 0, 0);
  return gfx_sprite_rect(inst->sprite, inst->x, inst->y);
}

gfx_rect_t
gfx_sprite_instance_old_rect(const gfx_sprite_instance_t GFX_PTR *inst) {
  if (!inst)
    return gfx_rect_make(0, 0, 0, 0);
  return gfx_sprite_rect(inst->sprite, inst->old_x, inst->old_y);
}

void gfx_dirty_init(gfx_dirty_list_t GFX_PTR *d, int screen_w, int screen_h) {
  if (!d)
    return;
  d->count = 0;
  d->full_redraw = 1;
  d->screen_w = screen_w;
  d->screen_h = screen_h;
}

void gfx_dirty_clear(gfx_dirty_list_t GFX_PTR *d) {
  if (!d)
    return;
  d->count = 0;
  d->full_redraw = 0;
}

void gfx_dirty_mark_full(gfx_dirty_list_t GFX_PTR *d) {
  if (!d)
    return;
  d->full_redraw = 1;
  d->count = 0;
}

void gfx_dirty_add_rect(gfx_dirty_list_t GFX_PTR *d, gfx_rect_t r) {
  if (!d || d->full_redraw)
    return;
  r = gfx_rect_clip(r, d->screen_w, d->screen_h);
  if (gfx_rect_empty(r))
    return;
  if (d->count >= GFX_DIRTY_MAX_RECTS) {
    gfx_dirty_mark_full(d);
    return;
  }
  d->rects[d->count++] = r;
}

void gfx_dirty_add_sprite_move(gfx_dirty_list_t GFX_PTR *d, int old_x,
                               int old_y, int new_x, int new_y, int w, int h,
                               int pad) {
  gfx_dirty_add_rect(
      d, gfx_rect_make(old_x - pad, old_y - pad, w + pad * 2, h + pad * 2));
  gfx_dirty_add_rect(
      d, gfx_rect_make(new_x - pad, new_y - pad, w + pad * 2, h + pad * 2));
}

void gfx_dirty_merge(gfx_dirty_list_t GFX_PTR *d) {
  int i, changed;
  if (!d || d->full_redraw)
    return;
  do {
    changed = 0;
    for (i = 0; i < d->count; ++i) {
      int j;
      for (j = i + 1; j < d->count; ++j) {
        if (gfx_rects_touch_or_overlap(d->rects[i], d->rects[j])) {
          int k;
          d->rects[i] = gfx_rect_union(d->rects[i], d->rects[j]);
          for (k = j; k + 1 < d->count; ++k)
            d->rects[k] = d->rects[k + 1];
          --d->count;
          changed = 1;
          break;
        }
      }
      if (changed)
        break;
    }
  } while (changed);
}

unsigned long gfx_dirty_total_area(const gfx_dirty_list_t GFX_PTR *d) {
  int i;
  unsigned long total = 0;
  if (!d)
    return 0;
  if (d->full_redraw)
    return (unsigned long)d->screen_w * (unsigned long)d->screen_h;
  for (i = 0; i < d->count; ++i)
    total += (unsigned long)gfx_rect_area(d->rects[i]);
  return total;
}

void gfx_dirty_adapt_to_full(gfx_dirty_list_t GFX_PTR *d,
                             unsigned long threshold_pixels) {
  if (!d || d->full_redraw)
    return;
  if (gfx_dirty_total_area(d) >= threshold_pixels)
    gfx_dirty_mark_full(d);
}

void gfx_dirty_render(
    gfx_renderer_t GFX_PTR *r, const gfx_dirty_list_t GFX_PTR *d,
    void (*draw_scene)(gfx_renderer_t GFX_PTR *r, void GFX_PTR *scene_user),
    void GFX_PTR *scene_user, gfx_color_t clear_color, unsigned flags) {
  int i;
  if (!r || !d)
    return;
  if (d->full_redraw) {
    gfx_render_tiled_ex(r, draw_scene, scene_user, clear_color, flags);
    return;
  }
  for (i = 0; i < d->count; ++i) {
    gfx_rect_t rr = gfx_rect_clip(d->rects[i], r->width, r->height);
    if (!gfx_rect_empty(rr))
      gfx_render_tiled_region_ex(r, rr.x, rr.y, rr.w, rr.h, draw_scene,
                                 scene_user, clear_color, flags);
  }
}

void gfx_sprites_mark_dirty(gfx_dirty_list_t GFX_PTR *d,
                            const gfx_sprite_instance_t GFX_PTR *sprites,
                            int count, int pad) {
  int i;
  if (!d || !sprites)
    return;
  for (i = 0; i < count; ++i) {
    const gfx_sprite_instance_t GFX_PTR *s = sprites + i;
    if (!s->sprite)
      continue;
    gfx_dirty_add_sprite_move(d, s->old_x, s->old_y, s->x, s->y,
                              s->sprite->width, s->sprite->height, pad);
  }
}

void gfx_sprites_draw_intersecting(gfx_renderer_t GFX_PTR *r,
                                   const gfx_sprite_instance_t GFX_PTR *sprites,
                                   int count, gfx_rect_t region,
                                   gfx_blit_stats_t GFX_PTR *stats) {
  int i;
  if (!r || !sprites)
    return;
  for (i = 0; i < count; ++i) {
    if (!sprites[i].visible || !sprites[i].sprite)
      continue;
    if (!gfx_rects_overlap(gfx_sprite_instance_rect(&sprites[i]), region))
      continue;
    gfx_blit_counted(r, sprites[i].sprite, sprites[i].x, sprites[i].y, stats);
  }
}

void gfx_sprites_draw_intersecting_grid(
    gfx_renderer_t GFX_PTR *r, const gfx_sprite_instance_t GFX_PTR *sprites,
    int count, gfx_rect_t region, gfx_blit_stats_t GFX_PTR *stats, int cell_w,
    int cell_h) {
  (void)cell_w;
  (void)cell_h;
  gfx_sprites_draw_intersecting(r, sprites, count, region, stats);
}
