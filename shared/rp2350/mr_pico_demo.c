#include "mr_pico_demo.h"

#include "gfx.h"
#include "mr_autodemo.h"
#include "mr_game_demo.h"
#include "mr_timestep.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/regs/clocks.h"
#include "hardware/timer.h"
#include "mr_pico_ili9341.h"
#include "mr_pico_screenshot.h"
#include "mr_pico_temporal_present.h"
#include "pico/stdlib.h"

#include <stdint.h>
#include <stdio.h>

#ifndef MR_SCREEN_W
#define MR_SCREEN_W 320
#endif

#ifndef MR_SCREEN_H
#define MR_SCREEN_H 240
#endif

#ifndef MR_TILE_H
#define MR_TILE_H 240
#endif

/*
 * 0 = deliberately serialized raw loop
 * 1 = ordinary full-frame/tiled DMA pipeline
 * 2 = temporal row-group DMA pipeline
 */
#ifndef MR_GAME_PRESENT_MODE
#define MR_GAME_PRESENT_MODE 1
#endif

#ifndef MR_GAME_LACE_BLOCK_H
#define MR_GAME_LACE_BLOCK_H 8
#endif

#ifndef MR_GAME_LACE_PHASES
#define MR_GAME_LACE_PHASES 4
#endif

#ifndef MR_PICO_GAME_SERIAL
#define MR_PICO_GAME_SERIAL 0
#endif

#ifndef MR_PICO_SYS_KHZ
#define MR_PICO_SYS_KHZ 0u
#endif

#ifndef MR_PICO_CLOCK_REQUIRED
#define MR_PICO_CLOCK_REQUIRED 0
#endif

#ifndef MR_PICO_PERI_FROM_SYS
#define MR_PICO_PERI_FROM_SYS 1
#endif

static gfx_color_t tile_buffer_a[MR_SCREEN_W * MR_TILE_H];
#if MR_GAME_PRESENT_MODE != 0
static gfx_color_t tile_buffer_b[MR_SCREEN_W * MR_TILE_H];
#endif

static gfx_renderer_t renderer;
static mr_game_demo_t game_demo;
static mr_timestep_t demo_step;
static unsigned long sim_ticks;
static mr_pico_screenshot_t screenshot_service;

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

#if MR_GAME_PRESENT_MODE == 2
static mr_pico_temporal_present_t temporal_present;
#endif

static unsigned long frame_counter;
static uint32_t start_fps_ms;
static uint32_t last_fps_ms;
static unsigned long last_fps_frame;

static const char *game_present_name(void) {
#if MR_GAME_PRESENT_MODE == 0
  return "raw";
#elif MR_GAME_PRESENT_MODE == 2
  return "lace";
#else
  return "pipeline";
#endif
}

static void draw_game_scene(gfx_renderer_t *r, void *user) {
  mr_game_demo_t *demo;
  demo = (mr_game_demo_t *)user;
  mr_game_demo_render(demo, r);
}

static void screenshot_wait_for_display(void *user) {
  (void)user;
#if MR_GAME_PRESENT_MODE == 2
  mr_pico_temporal_present_wait(&temporal_present, &renderer);
#else
  mr_pico_ili9341_flush_wait(&renderer, &lcd);
#endif
}

/* Same warm-boot recovery as the stress frontend. */
static void demo_recover_from_warm_boot(void) {
  dma_hw->abort = (uint32_t)~0u;
  while (dma_hw->abort)
    tight_loop_contents();
}

static int demo_configure_clocks(void) {
  int ok = 1;

#if MR_PICO_SYS_KHZ > 0
  ok = set_sys_clock_khz((uint32_t)MR_PICO_SYS_KHZ,
                         MR_PICO_CLOCK_REQUIRED ? true : false)
           ? 1
           : 0;
  sleep_ms(10);
#endif

#if MR_PICO_PERI_FROM_SYS
  {
    uint32_t sys_hz = clock_get_hz(clk_sys);
    if (sys_hz > 0) {
      clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                      sys_hz, sys_hz);
      sleep_ms(2);
    }
  }
#endif

  return ok;
}

void mr_pico_demo_main(void) {
  uint32_t now;
  mr_demo_input_t input;
  int clock_ok;

  demo_recover_from_warm_boot();
  clock_ok = demo_configure_clocks();

#if MR_PICO_GAME_SERIAL
  printf("MicroRender RP2350 shared game demo\n");
  printf("pins: MISO=%u CS=%u SCK=%u MOSI=%u RST=%u DC=%u\n",
         (unsigned)MR_LCD_PIN_MISO, (unsigned)MR_LCD_PIN_CS,
         (unsigned)MR_LCD_PIN_SCK, (unsigned)MR_LCD_PIN_MOSI,
         (unsigned)MR_LCD_PIN_RST, (unsigned)MR_LCD_PIN_DC);
  printf("screen: %dx%d RGB565 tile_h=%d requested_spi=%u Hz present=%s\n",
         MR_SCREEN_W, MR_SCREEN_H, MR_TILE_H, (unsigned)MR_LCD_SPI_BAUD,
         game_present_name());
  printf("clock: requested_sys=%u kHz actual_sys=%lu Hz peri=%lu Hz ok=%d\n",
         (unsigned)MR_PICO_SYS_KHZ, (unsigned long)clock_get_hz(clk_sys),
         (unsigned long)clock_get_hz(clk_peri), clock_ok);
  printf("usb screenshot service: SCREENSHOT, SHOT, PING, HELP\n");
#endif

  mr_pico_ili9341_init(&lcd);

#if MR_PICO_GAME_SERIAL
  printf("spi: requested=%u Hz actual=%u Hz\n", (unsigned)MR_LCD_SPI_BAUD,
         (unsigned)lcd.spi_baud_hz);
#endif

  mr_pico_ili9341_panel_init(&lcd);

#if MR_GAME_PRESENT_MODE == 2
  mr_pico_ili9341_fill_screen(&lcd, GFX_RGB565_BLACK, MR_SCREEN_W, MR_SCREEN_H);

  mr_pico_temporal_present_init(&temporal_present, &lcd, MR_SCREEN_H,
                                MR_GAME_LACE_BLOCK_H, MR_GAME_LACE_PHASES);

  gfx_init(&renderer, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           mr_pico_temporal_present_flush, &temporal_present);
  gfx_set_async_flush(&renderer, mr_pico_temporal_present_flush_begin,
                      mr_pico_temporal_present_flush_wait);
#else
  gfx_init(&renderer, MR_SCREEN_W, MR_SCREEN_H, tile_buffer_a, MR_TILE_H,
           mr_pico_ili9341_flush, &lcd);
#if MR_GAME_PRESENT_MODE != 0
  gfx_set_async_flush(&renderer, mr_pico_ili9341_flush_begin,
                      mr_pico_ili9341_flush_wait);
#endif
#endif

  mr_game_demo_init(&game_demo, MR_SCREEN_W, MR_SCREEN_H);
  mr_pico_screenshot_init(&screenshot_service, MR_SCREEN_W, MR_SCREEN_H,
                          tile_buffer_a, MR_TILE_H, GFX_RGB565_BLACK,
                          draw_game_scene, &game_demo,
                          screenshot_wait_for_display, 0);
  mr_autodemo_reset();

  frame_counter = 0;
  sim_ticks = 0;
  last_fps_frame = 0;
  start_fps_ms = to_ms_since_boot(get_absolute_time());
  last_fps_ms = start_fps_ms;
  mr_game_demo_set_fps10(&game_demo, 0ul, 0ul);

  mr_timestep_init(&demo_step, MR_GAME_TICK_HZ, 5);

  for (;;) {
    {
      int steps = mr_timestep_advance(&demo_step, (unsigned long)time_us_32());
      while (steps-- > 0) {
        mr_autodemo_input(sim_ticks, &input);
        mr_game_demo_tick(&game_demo, &input);
        ++sim_ticks;
      }
    }

#if MR_GAME_PRESENT_MODE == 0
    gfx_render_tiled(&renderer, draw_game_scene, &game_demo, GFX_RGB565_BLACK);
#elif MR_GAME_PRESENT_MODE == 2
    mr_pico_temporal_present_begin_frame(&temporal_present, frame_counter);
    gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_game_scene,
                               &game_demo, GFX_RGB565_BLACK, 0u);
#else
    gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_game_scene,
                               &game_demo, GFX_RGB565_BLACK, 0u);
#endif

    ++frame_counter;
    (void)mr_pico_screenshot_poll(&screenshot_service);

    now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_fps_ms) >= 500u) {
      unsigned long frames;
      unsigned long fps10;
      unsigned long avg_fps10;
      unsigned long sim_hz10;
      unsigned long sent_kb = 0ul;
      unsigned long phases = 1ul;
      unsigned long lace = 0ul;
      uint32_t window_ms;
      uint32_t total_ms;

      frames = frame_counter - last_fps_frame;
      window_ms = (uint32_t)(now - last_fps_ms);
      total_ms = (uint32_t)(now - start_fps_ms);
      fps10 = window_ms ? (frames * 10000ul) / (unsigned long)window_ms : 0ul;
      avg_fps10 =
          total_ms ? (frame_counter * 10000ul) / (unsigned long)total_ms : 0ul;
      sim_hz10 =
          total_ms ? (sim_ticks * 10000ul) / (unsigned long)total_ms : 0ul;

      mr_game_demo_set_fps10(&game_demo, fps10, avg_fps10);

#if MR_GAME_PRESENT_MODE == 2
      sent_kb =
          mr_pico_temporal_present_sent_bytes(&temporal_present) / 1024ul;
      phases = (unsigned long)MR_GAME_LACE_PHASES;
      lace = (unsigned long)MR_GAME_LACE_BLOCK_H;
#endif

#if MR_PICO_GAME_SERIAL
      printf(
          "game frame=%lu fps=%lu.%lu avg=%lu.%lu mode=%d mode_name=%s "
          "sim_ticks=%lu sim_hz=%lu.%lu sentKB=%lu sys=%lu peri=%lu spi=%u "
          "lace=%lu phases=%lu\n",
          frame_counter, fps10 / 10ul, fps10 % 10ul, avg_fps10 / 10ul,
          avg_fps10 % 10ul, MR_GAME_PRESENT_MODE, game_present_name(),
          sim_ticks, sim_hz10 / 10ul, sim_hz10 % 10ul, sent_kb,
          (unsigned long)clock_get_hz(clk_sys),
          (unsigned long)clock_get_hz(clk_peri), (unsigned)lcd.spi_baud_hz,
          lace, phases);
#endif

      last_fps_frame = frame_counter;
      last_fps_ms = now;
    }
  }
}
