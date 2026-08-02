#include "gfx.h"

#if GFX_ENABLE_TRIANGLES

static void gfx_tri_swap_int(int GFX_PTR *a, int GFX_PTR *b) {
  int t = *a;
  *a = *b;
  *b = t;
}

static void gfx_tri_span_direct(gfx_renderer_t GFX_PTR *r, int y, int x0,
                                int x1, gfx_color_t color) {
  gfx_color_t GFX_PTR *dst;
  int n;

  if (!r || !r->tile)
    return;

  if (x0 > x1)
    gfx_tri_swap_int(&x0, &x1);

  if (y < r->clip_y0 || y >= r->clip_y1)
    return;
  if (y < r->tile_y || y >= r->tile_y + r->tile_h)
    return;
  if (y < 0 || y >= r->height)
    return;

  if (x0 < r->clip_x0)
    x0 = r->clip_x0;
  if (x0 < r->tile_x)
    x0 = r->tile_x;
  if (x0 < 0)
    x0 = 0;

  if (x1 >= r->clip_x1)
    x1 = r->clip_x1 - 1;
  if (x1 >= r->tile_x + r->tile_w)
    x1 = r->tile_x + r->tile_w - 1;
  if (x1 >= r->width)
    x1 = r->width - 1;

  if (x1 < x0)
    return;

  dst = r->tile + (y - r->tile_y) * r->tile_stride + (x0 - r->tile_x);
  n = x1 - x0 + 1;
  while (n-- > 0)
    *dst++ = color;
}

static int gfx_tri_interp_x(int xa, int ya, int xb, int yb, int y) {
  int dy = yb - ya;
  long num;

  if (dy == 0)
    return xa;

  num = (long)(xb - xa) * (long)(y - ya);
  if (num >= 0)
    return xa + (int)((num + (dy / 2)) / (long)dy);
  return xa + (int)((num - (dy / 2)) / (long)dy);
}

static void gfx_tri_raster_sorted(gfx_renderer_t GFX_PTR *r, int x0, int y0,
                                  int x1, int y1, int x2, int y2,
                                  gfx_color_t color) {
  int y;
  int y_start;
  int y_end;

  y_start = y0;
  y_end = y2;

  if (y_start < r->clip_y0)
    y_start = r->clip_y0;
  if (y_start < r->tile_y)
    y_start = r->tile_y;
  if (y_start < 0)
    y_start = 0;

  if (y_end >= r->clip_y1)
    y_end = r->clip_y1 - 1;
  if (y_end >= r->tile_y + r->tile_h)
    y_end = r->tile_y + r->tile_h - 1;
  if (y_end >= r->height)
    y_end = r->height - 1;

  if (y_end < y_start)
    return;

  for (y = y_start; y <= y_end; ++y) {
    int xa;
    int xb;

    xa = gfx_tri_interp_x(x0, y0, x2, y2, y);
    if (y < y1)
      xb = gfx_tri_interp_x(x0, y0, x1, y1, y);
    else
      xb = gfx_tri_interp_x(x1, y1, x2, y2, y);

    gfx_tri_span_direct(r, y, xa, xb, color);
  }
}

void gfx_fill_triangle(gfx_renderer_t GFX_PTR *r, int x0, int y0, int x1,
                       int y1, int x2, int y2, gfx_color_t color) {
  int minx;
  int maxx;

  if (!r)
    return;

  if (y0 > y1) {
    gfx_tri_swap_int(&y0, &y1);
    gfx_tri_swap_int(&x0, &x1);
  }
  if (y1 > y2) {
    gfx_tri_swap_int(&y1, &y2);
    gfx_tri_swap_int(&x1, &x2);
  }
  if (y0 > y1) {
    gfx_tri_swap_int(&y0, &y1);
    gfx_tri_swap_int(&x0, &x1);
  }

  if (y0 == y2) {
    minx = x0;
    maxx = x0;
    if (x1 < minx)
      minx = x1;
    if (x2 < minx)
      minx = x2;
    if (x1 > maxx)
      maxx = x1;
    if (x2 > maxx)
      maxx = x2;
    gfx_tri_span_direct(r, y0, minx, maxx, color);
    return;
  }

  gfx_tri_raster_sorted(r, x0, y0, x1, y1, x2, y2, color);
}

#endif /* GFX_ENABLE_TRIANGLES */
