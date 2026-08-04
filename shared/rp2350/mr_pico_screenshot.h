#ifndef MR_PICO_SCREENSHOT_H
#define MR_PICO_SCREENSHOT_H

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MR_PICO_SCREENSHOT
#define MR_PICO_SCREENSHOT 1
#endif

/* Draw one complete logical frame into the renderer supplied by the screenshot
 * service. The callback must not present to the LCD; the service replaces the
 * renderer flush callback with a USB RGB565 stream. */
typedef void (*mr_pico_screenshot_draw_fn)(gfx_renderer_t *renderer,
                                           void *user);

/* Optional synchronization hook used before reusing a render buffer. Pico
 * frontends normally wait for an in-flight LCD DMA transfer here. */
typedef void (*mr_pico_screenshot_wait_fn)(void *user);

typedef struct mr_pico_screenshot {
  int width;
  int height;
  int tile_h;
  gfx_color_t *tile_buffer;
  gfx_color_t clear_color;
  mr_pico_screenshot_draw_fn draw;
  void *draw_user;
  mr_pico_screenshot_wait_fn wait;
  void *wait_user;
  char command[48];
  unsigned command_len;
} mr_pico_screenshot_t;

/* Register the logical frame provider for one Pico application. This API is
 * shared by the game, all stress modes, lcdtest, and future Pico frontends. */
void mr_pico_screenshot_init(mr_pico_screenshot_t *shot, int width, int height,
                             gfx_color_t *tile_buffer, int tile_h,
                             gfx_color_t clear_color,
                             mr_pico_screenshot_draw_fn draw, void *draw_user,
                             mr_pico_screenshot_wait_fn wait,
                             void *wait_user);

/* Poll USB CDC without blocking. Returns 1 after a screenshot was transmitted,
 * otherwise 0. Safe to call once per frame. */
int mr_pico_screenshot_poll(mr_pico_screenshot_t *shot);

#ifdef __cplusplus
}
#endif

#endif /* MR_PICO_SCREENSHOT_H */
