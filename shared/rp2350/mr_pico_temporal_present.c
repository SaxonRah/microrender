#include "mr_pico_temporal_present.h"

#include <string.h>

static int temporal_span(const mr_pico_temporal_present_t *ctx, int tile_y,
                         int tile_h, int *send_y, int *send_h,
                         int *pixel_row) {
  int period;
  int tile_end;
  int base;
  int sy;
  int ey;

  if (!ctx || !send_y || !send_h || !pixel_row || tile_h <= 0 ||
      ctx->block_h <= 0 || ctx->phases <= 0 || ctx->view_h <= 0)
    return 0;

  period = ctx->block_h * ctx->phases;
  if (period <= 0)
    return 0;

  tile_end = tile_y + tile_h;
  base = (tile_y / period) * period;
  sy = base + ctx->phase * ctx->block_h;

  while (sy + ctx->block_h <= tile_y)
    sy += period;

  if (sy >= tile_end || sy >= ctx->view_h)
    return 0;

  ey = sy + ctx->block_h;
  if (sy < tile_y)
    sy = tile_y;
  if (ey > tile_end)
    ey = tile_end;
  if (ey > ctx->view_h)
    ey = ctx->view_h;
  if (ey <= sy)
    return 0;

  *send_y = sy;
  *send_h = ey - sy;
  *pixel_row = sy - tile_y;
  return 1;
}

void mr_pico_temporal_present_init(mr_pico_temporal_present_t *ctx,
                                   mr_pico_ili9341_t *lcd, int view_h,
                                   int block_h, int phases) {
  if (!ctx)
    return;

  memset(ctx, 0, sizeof(*ctx));
  ctx->lcd = lcd;
  ctx->view_h = view_h > 0 ? view_h : 1;
  ctx->block_h = block_h > 0 ? block_h : 1;
  ctx->phases = phases > 0 ? phases : 1;
  ctx->phase = 0;
}

void mr_pico_temporal_present_begin_frame(mr_pico_temporal_present_t *ctx,
                                          unsigned long frame_index) {
  if (!ctx)
    return;
  ctx->phase =
      (ctx->phases <= 1) ? 0 : (int)(frame_index % (unsigned long)ctx->phases);
}

int mr_pico_temporal_present_phase(const mr_pico_temporal_present_t *ctx) {
  return ctx ? ctx->phase : 0;
}

unsigned long
mr_pico_temporal_present_sent_bytes(const mr_pico_temporal_present_t *ctx) {
  return ctx ? ctx->total_sent_bytes : 0ul;
}

void mr_pico_temporal_present_flush(gfx_renderer_t *r, int x, int y, int w,
                                    int h, const gfx_color_t *pixels,
                                    void *user) {
  mr_pico_temporal_present_t *ctx =
      (mr_pico_temporal_present_t *)user;
  int send_y;
  int send_h;
  int pixel_row;

  if (!ctx || !ctx->lcd || !pixels || w <= 0 || h <= 0)
    return;

  if (!temporal_span(ctx, y, h, &send_y, &send_h, &pixel_row))
    return;

  pixels += (long)pixel_row * (long)w;
  mr_pico_ili9341_flush(r, x, send_y, w, send_h, pixels, ctx->lcd);
  ctx->total_sent_bytes += (unsigned long)w * (unsigned long)send_h * 2ul;
}

void mr_pico_temporal_present_flush_begin(gfx_renderer_t *r, int x, int y,
                                          int w, int h,
                                          const gfx_color_t *pixels,
                                          void *user) {
  mr_pico_temporal_present_t *ctx =
      (mr_pico_temporal_present_t *)user;
  int send_y;
  int send_h;
  int pixel_row;

  if (!ctx)
    return;

  ctx->async_active = 0;

  if (!ctx->lcd || !pixels || w <= 0 || h <= 0)
    return;

  if (!temporal_span(ctx, y, h, &send_y, &send_h, &pixel_row))
    return;

  pixels += (long)pixel_row * (long)w;
  mr_pico_ili9341_flush_begin(r, x, send_y, w, send_h, pixels, ctx->lcd);
  ctx->async_active = 1;
  ctx->total_sent_bytes += (unsigned long)w * (unsigned long)send_h * 2ul;
}

void mr_pico_temporal_present_flush_wait(gfx_renderer_t *r, void *user) {
  mr_pico_temporal_present_t *ctx =
      (mr_pico_temporal_present_t *)user;

  if (!ctx || !ctx->lcd || !ctx->async_active)
    return;

  mr_pico_ili9341_flush_wait(r, ctx->lcd);
  ctx->async_active = 0;
}

void mr_pico_temporal_present_wait(mr_pico_temporal_present_t *ctx,
                                   gfx_renderer_t *r) {
  if (!ctx || !ctx->lcd || !ctx->async_active)
    return;

  mr_pico_ili9341_flush_wait(r, ctx->lcd);
  ctx->async_active = 0;
}
