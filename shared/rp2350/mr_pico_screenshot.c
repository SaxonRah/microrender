#include "mr_pico_screenshot.h"

#if MR_PICO_SCREENSHOT

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

static void screenshot_stream_flush(gfx_renderer_t *renderer, int x, int y,
                                    int w, int h,
                                    const gfx_color_t *pixels, void *user) {
  mr_pico_screenshot_t *shot;
  int row;
  int stride;

  shot = (mr_pico_screenshot_t *)user;
  if (!shot || !pixels || w <= 0 || h <= 0)
    return;
  if (x != 0 || w != shot->width || y < 0 || y + h > shot->height)
    return;

  stride = renderer ? renderer->tile_stride : w;
  for (row = 0; row < h; ++row) {
    fwrite((const void *)(pixels + row * stride), sizeof(gfx_color_t),
           (size_t)w, stdout);
  }
}

static int screenshot_send(mr_pico_screenshot_t *shot) {
  gfx_renderer_t capture;
  size_t byte_count;

  if (!shot || !shot->tile_buffer || !shot->draw || shot->width <= 0 ||
      shot->height <= 0 || shot->tile_h <= 0) {
    return 0;
  }

  if (!stdio_usb_connected())
    return 0;

  if (shot->wait)
    shot->wait(shot->wait_user);

  byte_count = (size_t)shot->width * (size_t)shot->height *
               sizeof(gfx_color_t);

  /* MRSHOT1 is intentionally simple and app-independent. The receiver reads
   * exactly byte_count bytes after this line, so no diagnostic text is emitted
   * until the complete image has been streamed. */
  printf("MRSHOT1 %d %d %lu\n", shot->width, shot->height,
         (unsigned long)byte_count);
  fflush(stdout);

  gfx_init(&capture, shot->width, shot->height, shot->tile_buffer, shot->tile_h,
           screenshot_stream_flush, shot);
  gfx_render_tiled(&capture, shot->draw, shot->draw_user, shot->clear_color);
  fflush(stdout);
  return 1;
}

static int screenshot_handle_line(mr_pico_screenshot_t *shot,
                                  const char *line) {
  if (strcmp(line, "SCREENSHOT") == 0 || strcmp(line, "SHOT") == 0) {
    return screenshot_send(shot);
  }

  if (strcmp(line, "PING") == 0) {
    printf("MRPICO1 %d %d RGB565 SCREENSHOT\n", shot->width, shot->height);
    return 0;
  }

  if (strcmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
    printf("commands: SCREENSHOT, SHOT, PING, HELP\n");
    return 0;
  }

  if (line[0] != '\0')
    printf("unknown command: %s\n", line);
  return 0;
}

void mr_pico_screenshot_init(mr_pico_screenshot_t *shot, int width, int height,
                             gfx_color_t *tile_buffer, int tile_h,
                             gfx_color_t clear_color,
                             mr_pico_screenshot_draw_fn draw, void *draw_user,
                             mr_pico_screenshot_wait_fn wait,
                             void *wait_user) {
  if (!shot)
    return;

  memset(shot, 0, sizeof(*shot));
  shot->width = width;
  shot->height = height;
  shot->tile_h = tile_h;
  shot->tile_buffer = tile_buffer;
  shot->clear_color = clear_color;
  shot->draw = draw;
  shot->draw_user = draw_user;
  shot->wait = wait;
  shot->wait_user = wait_user;
}

int mr_pico_screenshot_poll(mr_pico_screenshot_t *shot) {
  int ch;

  if (!shot || !stdio_usb_connected())
    return 0;

  for (;;) {
    ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT)
      break;

    if (ch == '\r' || ch == '\n') {
      int sent;
      shot->command[shot->command_len] = '\0';
      shot->command_len = 0u;
      sent = screenshot_handle_line(shot, shot->command);
      if (sent)
        return 1;
    } else if (ch >= 32 && ch <= 126) {
      if (shot->command_len + 1u < sizeof(shot->command)) {
        shot->command[shot->command_len++] = (char)ch;
      } else {
        shot->command_len = 0u;
      }
    }
  }

  return 0;
}

#else /* MR_PICO_SCREENSHOT */

#include <string.h>

void mr_pico_screenshot_init(mr_pico_screenshot_t *shot, int width, int height,
                             gfx_color_t *tile_buffer, int tile_h,
                             gfx_color_t clear_color,
                             mr_pico_screenshot_draw_fn draw, void *draw_user,
                             mr_pico_screenshot_wait_fn wait,
                             void *wait_user) {
  (void)width;
  (void)height;
  (void)tile_buffer;
  (void)tile_h;
  (void)clear_color;
  (void)draw;
  (void)draw_user;
  (void)wait;
  (void)wait_user;
  if (shot)
    memset(shot, 0, sizeof(*shot));
}

int mr_pico_screenshot_poll(mr_pico_screenshot_t *shot) {
  (void)shot;
  return 0;
}

#endif /* MR_PICO_SCREENSHOT */
