#include "mr_pico_demo.h"

#include "gfx.h"
#include "mr_autodemo.h"
#include "mr_game_demo.h"
#include "mr_pico_ili9341.h"

#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef MR_SCREEN_W
#define MR_SCREEN_W 320
#endif

#ifndef MR_SCREEN_H
#define MR_SCREEN_H 240
#endif

#ifndef MR_TILE_H
#define MR_TILE_H 16
#endif

static gfx_color_t tile_buffer_a[MR_SCREEN_W * MR_TILE_H];
static gfx_color_t tile_buffer_b[MR_SCREEN_W * MR_TILE_H];
static gfx_renderer_t renderer;
static mr_game_demo_t game_demo;

/* 320x240 RGB565 = 153,600 bytes. Used only for USB screenshot requests. */
static gfx_color_t screenshot_frame[MR_SCREEN_W * MR_SCREEN_H];

static mr_pico_ili9341_t lcd = {MR_LCD_SPI,
                                0u,
                                MR_LCD_PIN_MISO,
                                MR_LCD_PIN_CS,
                                MR_LCD_PIN_SCK,
                                MR_LCD_PIN_MOSI,
                                MR_LCD_PIN_RST,
                                MR_LCD_PIN_DC,
                                MR_LCD_SPI_BAUD,
                                0,
                                0,
                                0u};

static unsigned long frame_counter;
static uint32_t last_fps_ms;
static unsigned long last_fps_frame;
static char usb_cmd[40];
static unsigned usb_cmd_len;

static void draw_game_scene(gfx_renderer_t *r, void *user) {
  mr_game_demo_t *demo;
  demo = (mr_game_demo_t *)user;
  mr_game_demo_render(demo, r);
}

static void screenshot_capture_flush(gfx_renderer_t *r, int x, int y, int w,
                                     int h, const gfx_color_t *pixels,
                                     void *user) {
  int row;

  (void)r;
  (void)user;

  if (!pixels || w <= 0 || h <= 0)
    return;
  if (x < 0 || y < 0 || x + w > MR_SCREEN_W || y + h > MR_SCREEN_H)
    return;

  for (row = 0; row < h; ++row) {
    memcpy(&screenshot_frame[(y + row) * MR_SCREEN_W + x], &pixels[row * w],
           (size_t)w * sizeof(gfx_color_t));
  }
}

static void send_screenshot(void) {
  gfx_renderer_t cap;
  size_t bytes;

  bytes = (size_t)MR_SCREEN_W * (size_t)MR_SCREEN_H * sizeof(gfx_color_t);

  mr_pico_ili9341_flush_wait(&renderer, &lcd);

  gfx_init(&cap, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           screenshot_capture_flush, 0);

  gfx_render_tiled(&cap, draw_game_scene, &game_demo, GFX_RGB565_BLACK);

  printf("MRSHOT1 %d %d %lu\n", MR_SCREEN_W, MR_SCREEN_H, (unsigned long)bytes);
  fflush(stdout);
  fwrite((const void *)screenshot_frame, 1, bytes, stdout);
  fflush(stdout);
}

static int usb_line_ready(const char *line) {
  if (strcmp(line, "SCREENSHOT") == 0 || strcmp(line, "SHOT") == 0) {
    return 1;
  }

  if (strcmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
    printf("commands: SCREENSHOT, SHOT, HELP\n");
    return 0;
  }

  if (line[0] != '\0') {
    printf("unknown command: %s\n", line);
  }

  return 0;
}

static int poll_usb_screenshot_request(void) {
  int ch;

  for (;;) {
    ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) {
      break;
    }

    if (ch == '\r' || ch == '\n') {
      usb_cmd[usb_cmd_len] = '\0';
      usb_cmd_len = 0;
      if (usb_line_ready(usb_cmd)) {
        return 1;
      }
    } else if (ch >= 32 && ch <= 126) {
      if (usb_cmd_len + 1u < sizeof(usb_cmd)) {
        usb_cmd[usb_cmd_len++] = (char)ch;
      } else {
        usb_cmd_len = 0;
      }
    }
  }

  return 0;
}

void mr_pico_demo_main(void) {
  uint32_t now;
  mr_demo_input_t input;

  printf("MicroRender RP2350 shared game demo\n");
  printf("pins: MISO=%u CS=%u SCK=%u MOSI=%u RST=%u DC=%u\n",
         (unsigned)MR_LCD_PIN_MISO, (unsigned)MR_LCD_PIN_CS,
         (unsigned)MR_LCD_PIN_SCK, (unsigned)MR_LCD_PIN_MOSI,
         (unsigned)MR_LCD_PIN_RST, (unsigned)MR_LCD_PIN_DC);
  printf("screen: %dx%d tile_h=%d spi=%u Hz\n", MR_SCREEN_W, MR_SCREEN_H,
         MR_TILE_H, (unsigned)MR_LCD_SPI_BAUD);
  printf("usb command: SCREENSHOT\n");

  mr_pico_ili9341_init(&lcd);
  mr_pico_ili9341_panel_init(&lcd);

  gfx_init(&renderer, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           mr_pico_ili9341_flush, &lcd);
  gfx_set_async_flush(&renderer, mr_pico_ili9341_flush_begin,
                      mr_pico_ili9341_flush_wait);

  mr_game_demo_init(&game_demo, MR_SCREEN_W, MR_SCREEN_H);
  mr_autodemo_reset();

  frame_counter = 0;
  last_fps_frame = 0;
  last_fps_ms = to_ms_since_boot(get_absolute_time());

  for (;;) {
    mr_autodemo_input(frame_counter, &input);
    mr_game_demo_tick(&game_demo, &input);

    gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_game_scene,
                               &game_demo, GFX_RGB565_BLACK, 0u);

    ++frame_counter;

    if (poll_usb_screenshot_request()) {
      send_screenshot();
    }

    now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_fps_ms) >= 1000u) {
      unsigned long frames;
      frames = frame_counter - last_fps_frame;
      printf("frame=%lu fps=%lu\n", frame_counter, frames);
      last_fps_frame = frame_counter;
      last_fps_ms = now;
    }
  }
}
