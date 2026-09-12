#ifndef MR_PICO_TEMPORAL_PRESENT_H
#define MR_PICO_TEMPORAL_PRESENT_H

#include "gfx.h"
#include "mr_pico_ili9341.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiled temporal presenter.
 *
 * One logical frame is divided into N row-group phases. A render tile is one
 * complete phase period:
 *
 *     tile_h = block_h * phases
 *
 * Only the current phase's contiguous block is sent from each rendered tile.
 * gfx_render_tiled_pipelined() therefore overlaps that LCD DMA with rasterizing
 * the next tile into the second tile buffer.
 *
 * This keeps Core 1 free and requires only two small render tiles rather than a
 * pair of full-screen framebuffers.
 */
typedef struct mr_pico_temporal_present {
  mr_pico_ili9341_t *lcd;
  int view_h;
  int block_h;
  int phases;
  int phase;
  int async_active;
  unsigned long total_sent_bytes;
} mr_pico_temporal_present_t;

void mr_pico_temporal_present_init(mr_pico_temporal_present_t *ctx,
                                   mr_pico_ili9341_t *lcd, int view_h,
                                   int block_h, int phases);

void mr_pico_temporal_present_begin_frame(mr_pico_temporal_present_t *ctx,
                                          unsigned long frame_index);

int mr_pico_temporal_present_phase(const mr_pico_temporal_present_t *ctx);
unsigned long
mr_pico_temporal_present_sent_bytes(const mr_pico_temporal_present_t *ctx);

void mr_pico_temporal_present_flush(gfx_renderer_t *r, int x, int y, int w,
                                    int h, const gfx_color_t *pixels,
                                    void *user);

void mr_pico_temporal_present_flush_begin(gfx_renderer_t *r, int x, int y,
                                          int w, int h,
                                          const gfx_color_t *pixels,
                                          void *user);

void mr_pico_temporal_present_flush_wait(gfx_renderer_t *r, void *user);

void mr_pico_temporal_present_wait(mr_pico_temporal_present_t *ctx,
                                   gfx_renderer_t *r);

#ifdef __cplusplus
}
#endif

#endif
