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
#define MR_TILE_H 240
#endif

/* 0 = deliberately serialized raw loop: clear/draw, then send the whole frame.
   1 = optimized DMA pipeline: send frame N while drawing frame N+1. */
#ifndef MR_GAME_PRESENT_MODE
#define MR_GAME_PRESENT_MODE 1
#endif
#ifndef MR_PICO_GAME_SERIAL
#define MR_PICO_GAME_SERIAL 0
#endif

static gfx_color_t tile_buffer_a[MR_SCREEN_W * MR_TILE_H];
#if MR_GAME_PRESENT_MODE != 0
static gfx_color_t tile_buffer_b[MR_SCREEN_W * MR_TILE_H];
#endif
static gfx_renderer_t renderer;
static mr_game_demo_t game_demo;

static mr_pico_ili9341_t lcd = {
    .spi = MR_LCD_SPI,
    .dma_chan = 0u,
    .pin_miso = MR_LCD_PIN_MISO,
    .pin_cs = MR_LCD_PIN_CS,
    .pin_sck = MR_LCD_PIN_SCK,
    .pin_mosi = MR_LCD_PIN_MOSI,
    .pin_rst = MR_LCD_PIN_RST,
    .pin_dc = MR_LCD_PIN_DC,
    .spi_baud_hz = MR_LCD_SPI_BAUD,
    .x_offset = 0,
    .y_offset = 0,
    .dma_active = 0u,
    .spi_format_bits = 0u,
    .dma_cfg16 = {0}};

static unsigned long frame_counter;
static uint32_t start_fps_ms;
static uint32_t last_fps_ms;
static unsigned long last_fps_frame;
#if MR_PICO_GAME_SERIAL
static char usb_cmd[40];
static unsigned usb_cmd_len;
#endif

static void draw_game_scene(gfx_renderer_t *r, void *user) {
  mr_game_demo_t *demo;
  demo = (mr_game_demo_t *)user;
  mr_game_demo_render(demo, r);
}

#if MR_PICO_GAME_SERIAL
static void screenshot_stream_flush(gfx_renderer_t *r, int x, int y, int w,
                                    int h, const gfx_color_t *pixels,
                                    void *user) {
  int row;
  int stride;

  (void)user;
  if (!pixels || w <= 0 || h <= 0)
    return;
  if (x != 0 || w != MR_SCREEN_W || y < 0 || y + h > MR_SCREEN_H)
    return;

  stride = r ? r->tile_stride : w;
  for (row = 0; row < h; ++row) {
    fwrite((const void *)(pixels + row * stride), sizeof(gfx_color_t),
           (size_t)w, stdout);
  }
}

static void send_screenshot(void) {
  gfx_renderer_t cap;
  size_t bytes;

  bytes = (size_t)MR_SCREEN_W * (size_t)MR_SCREEN_H * sizeof(gfx_color_t);

  mr_pico_ili9341_flush_wait(&renderer, &lcd);

  /* The renderer flushes full-width tiles from top to bottom, so the screenshot
     can be streamed after its header instead of reserving a third 150 KiB
     framebuffer in RP2350 SRAM. */
  printf("MRSHOT1 %d %d %lu\n", MR_SCREEN_W, MR_SCREEN_H,
         (unsigned long)bytes);
  fflush(stdout);

  gfx_init(&cap, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           screenshot_stream_flush, 0);
  gfx_render_tiled(&cap, draw_game_scene, &game_demo, GFX_RGB565_BLACK);
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

#endif /* MR_PICO_GAME_SERIAL */

void mr_pico_demo_main(void) {
  uint32_t now;
  mr_demo_input_t input;

#if MR_PICO_GAME_SERIAL
  printf("MicroRender RP2350 shared game demo\n");
  printf("pins: MISO=%u CS=%u SCK=%u MOSI=%u RST=%u DC=%u\n",
         (unsigned)MR_LCD_PIN_MISO, (unsigned)MR_LCD_PIN_CS,
         (unsigned)MR_LCD_PIN_SCK, (unsigned)MR_LCD_PIN_MOSI,
         (unsigned)MR_LCD_PIN_RST, (unsigned)MR_LCD_PIN_DC);
  printf("screen: %dx%d RGB565 tile_h=%d spi=%u Hz present=%s\n",
         MR_SCREEN_W, MR_SCREEN_H, MR_TILE_H, (unsigned)MR_LCD_SPI_BAUD,
         MR_GAME_PRESENT_MODE == 0 ? "raw-serialized" : "dma-pipelined");
  printf("usb command: SCREENSHOT\n");
#endif

  mr_pico_ili9341_init(&lcd);
  mr_pico_ili9341_panel_init(&lcd);

  gfx_init(&renderer, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           mr_pico_ili9341_flush, &lcd);
#if MR_GAME_PRESENT_MODE != 0
  gfx_set_async_flush(&renderer, mr_pico_ili9341_flush_begin,
                      mr_pico_ili9341_flush_wait);
#endif

  mr_game_demo_init(&game_demo, MR_SCREEN_W, MR_SCREEN_H);
  mr_autodemo_reset();

  frame_counter = 0;
  last_fps_frame = 0;
  start_fps_ms = to_ms_since_boot(get_absolute_time());
  last_fps_ms = start_fps_ms;
  mr_game_demo_set_fps10(&game_demo, 0ul, 0ul);

  for (;;) {
    mr_autodemo_input(frame_counter, &input);
    mr_game_demo_tick(&game_demo, &input);

#if MR_GAME_PRESENT_MODE == 0
    /* Baseline path kept intentionally: clear/draw the complete frame, then
       synchronously send it, then begin the next loop iteration. */
    gfx_render_tiled(&renderer, draw_game_scene, &game_demo, GFX_RGB565_BLACK);
#else
    gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_game_scene,
                               &game_demo, GFX_RGB565_BLACK, 0u);
#endif

    ++frame_counter;

#if MR_PICO_GAME_SERIAL
    if (poll_usb_screenshot_request()) {
      send_screenshot();
    }
#endif

    now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_fps_ms) >= 500u) {
      unsigned long frames;
      unsigned long fps10;
      unsigned long avg_fps10;
      uint32_t window_ms;
      uint32_t total_ms;
      frames = frame_counter - last_fps_frame;
      window_ms = (uint32_t)(now - last_fps_ms);
      total_ms = (uint32_t)(now - start_fps_ms);
      fps10 = window_ms ? (frames * 10000ul) / (unsigned long)window_ms : 0ul;
      avg_fps10 = total_ms ? (frame_counter * 10000ul) / (unsigned long)total_ms
                           : 0ul;
      mr_game_demo_set_fps10(&game_demo, fps10, avg_fps10);
#if MR_PICO_GAME_SERIAL
      printf("frame=%lu fps=%lu.%lu avg=%lu.%lu mode=%s\n", frame_counter,
             fps10 / 10ul, fps10 % 10ul, avg_fps10 / 10ul,
             avg_fps10 % 10ul,
             MR_GAME_PRESENT_MODE == 0 ? "raw" : "pipeline");
#endif
      last_fps_frame = frame_counter;
      last_fps_ms = now;
    }
  }
}
