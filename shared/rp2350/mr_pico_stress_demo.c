#include "mr_pico_stress_demo.h"
/* MR_PICO_PRESENT_CORE1 arrives from the build system like every other MR_*
   option, so it is already visible here. The #ifndef fallback further down
   only covers builds that never set it, where this evaluates to 0 and the
   second-core presenter is compiled out entirely. */
#if MR_PICO_PRESENT_CORE1
#include "hardware/sync.h"
#include "pico/multicore.h"
#endif
#include "mr_strbuf.h"
#include "gfx.h"
#include "mr_timestep.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/timer.h"
#include "mr_pico_ili9341.h"
#include "mr_pico_screenshot.h"
#include "mr_stress_test.h"
#include "pico/stdlib.h"
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#ifndef MR_SCREEN_W
#define MR_SCREEN_W 320
#endif

#ifndef MR_SCREEN_H
#define MR_SCREEN_H 240
#endif

#ifndef MR_VIEW_H
#define MR_VIEW_H MR_SCREEN_H
#endif

#ifndef MR_TILE_H
#define MR_TILE_H 16
#endif

#ifndef MR_STRESS_SPRITES
#define MR_STRESS_SPRITES 512
#endif

#ifndef MR_STRESS_TARGET_FPS
#define MR_STRESS_TARGET_FPS 120
#endif

/*
 * 0 = visible/full: render every frame and flush every tile band to LCD.
 * 1 = render-only: show one proof frame, then render with null flush;
 *     read FPS over USB serial.
 * 2 = every-N: render every frame and full-flush LCD every
 *     MR_STRESS_PICO_LCD_EVERY frames.
 * 3 = dirty: render every frame and send only changed row spans.
 *     Best paired with MR_STRESS_FIXED_CAMERA=1.
 * 4 = lcdtest: no sprite/tile stress, just full-screen LCD DMA flush test
 *     using one 320x240 transaction.  This is the display ceiling.
 * 5 = dirtyrect: full-resolution output, but render to a full 320x240
 *     shadow frame, compare against the previous frame, coalesce changed rows
 *     into compact rectangles, and flush only those rectangles.  If too much
 *     changes, it falls back to one full-frame DMA flush.
 * 6 = lace: full 320x240 render every frame, but present alternating row
 *     groups each frame.  This keeps full physical resolution and cuts the
 *     SPI payload roughly in half, at the cost of temporal combing/stale rows.
 * 7 = raw: deliberately serialized baseline. Clear/draw a complete frame,
 *     synchronously send the whole frame, then begin the next iteration.
 */
#ifndef MR_STRESS_PICO_FLUSH_MODE
#define MR_STRESS_PICO_FLUSH_MODE 0
#endif

#ifndef MR_STRESS_PICO_LCD_EVERY
#define MR_STRESS_PICO_LCD_EVERY 4
#endif

#ifndef MR_STRESS_STATS_RATE
#define MR_STRESS_STATS_RATE 8
#endif

#ifndef MR_STRESS_PICO_DIRTY_MERGE_GAP
#define MR_STRESS_PICO_DIRTY_MERGE_GAP 4
#endif

#ifndef MR_STRESS_PICO_DIRTY_RECT_MAX_H
#define MR_STRESS_PICO_DIRTY_RECT_MAX_H 16
#endif

#ifndef MR_STRESS_PICO_DIRTY_FULL_THRESHOLD_PCT
#define MR_STRESS_PICO_DIRTY_FULL_THRESHOLD_PCT 70
#endif

#ifndef MR_STRESS_PICO_LACE_BLOCK_H
#define MR_STRESS_PICO_LACE_BLOCK_H 4
#endif

#ifndef MR_STRESS_PICO_PRINT_MS
#define MR_STRESS_PICO_PRINT_MS 500u
#endif

#ifndef MR_STRESS_PICO_SERIAL
#if MR_STRESS_PICO_FLUSH_MODE == 1
#define MR_STRESS_PICO_SERIAL 1
#else
#define MR_STRESS_PICO_SERIAL 0
#endif
#endif

#if MR_STRESS_PICO_SERIAL
#include "pico/stdio_usb.h"
#include <stdio.h>
#endif

#ifndef MR_STRESS_PICO_WAIT_USB_MS
#define MR_STRESS_PICO_WAIT_USB_MS 0u
#endif

/*
 * Pico-only diagnostic footer.  Keep this optional because the footer itself
 * writes right at the active viewport edge in short-viewport tests.  Disable
 * it with diag=0 for clean presentation/performance runs after the clock/SPI
 * values are known.
 */
#ifndef MR_STRESS_PICO_DIAG
#define MR_STRESS_PICO_DIAG 1
#endif

#ifndef MR_PICO_SYS_KHZ
#define MR_PICO_SYS_KHZ 170000u
#endif

#ifndef MR_PICO_CLOCK_REQUIRED
#define MR_PICO_CLOCK_REQUIRED 0
#endif

/*
 * After set_sys_clock_khz(), clk_peri can remain at 48 MHz.  SPI baud is
 * derived from clk_peri, so requested LCD SPI rates above 24 MHz collapse to
 * 24 MHz unless we explicitly re-source clk_peri from clk_sys.
 */
#ifndef MR_PICO_PERI_FROM_SYS
#define MR_PICO_PERI_FROM_SYS 1
#endif

/*
 * Optional split-clock overclock for LCD throughput.
 *
 * The RP-series SPI baud generator can only divide clk_peri by
 * prescale * postdiv. At clk_peri=300 MHz, requests between 75 and
 * 150 MHz still commonly land on 75 MHz. Setting the system PLL to
 * 340 MHz, attaching clk_peri directly to that PLL, then dividing
 * clk_sys back down to 300 MHz gives an exact 85 MHz SPI clock while
 * keeping the CPU/bus at the known 300 MHz setting.
 *
 * 0 = disabled; use MR_PICO_SYS_KHZ for both sys PLL and clk_sys.
 */
#ifndef MR_PICO_PERI_PLL_KHZ
#define MR_PICO_PERI_PLL_KHZ 0u
#endif

#ifndef MR_PICO_FRAME_PIPELINE
#define MR_PICO_FRAME_PIPELINE 1
#endif

/*
 * A full 320x240 buffer costs 150 KiB.  The v9/v10 diagnostic code always
 * reserved one even for normal visible/render modes, which made large tile
 * bands unsafe.  Only dirty-span mode and lcdtest need a full-screen backing
 * buffer; normal visible/render modes only need the two render bands.
 */
#if (MR_STRESS_PICO_FLUSH_MODE == 3) || (MR_STRESS_PICO_FLUSH_MODE == 4) || \
    (MR_STRESS_PICO_FLUSH_MODE == 5)
#define MR_STRESS_NEEDS_FULL_FRAME 1
#else
#define MR_STRESS_NEEDS_FULL_FRAME 0
#endif

#if MR_STRESS_PICO_FLUSH_MODE == 5
#define MR_STRESS_NEEDS_DIRTY_RECT 1
#else
#define MR_STRESS_NEEDS_DIRTY_RECT 0
#endif

#if MR_VIEW_H > MR_SCREEN_H
#undef MR_VIEW_H
#define MR_VIEW_H MR_SCREEN_H
#endif
#if MR_VIEW_H < 32
#undef MR_VIEW_H
#define MR_VIEW_H 32
#endif

/*
 * Optional second-core presenter for lace.
 *
 * Lace renders a complete frame and then sends alternating row groups with the
 * blocking flush, so ~5 ms of rasterization and ~8 ms of SPI transfer run
 * strictly one after the other. The transfer cannot be handed to DMA alone,
 * because each row group needs its own CASET/RASET/RAMWR window written in
 * 8-bit mode, and DMA has no way to wait for the shifter to drain before
 * toggling D/C.
 *
 * Core 1 can do that waiting instead. It owns the panel for a whole frame
 * while core 0 renders the next one into the other buffer.
 *
 * Cost: this reinstates the 150 KiB second buffer that lace currently skips,
 * for 300 KiB of the RP2350's 520 KiB. Off by default for that reason.
 */
#ifndef MR_PICO_PRESENT_CORE1
#define MR_PICO_PRESENT_CORE1 0
#endif

#if MR_PICO_PRESENT_CORE1 && (MR_STRESS_PICO_FLUSH_MODE == 6) &&               \
    (MR_TILE_H >= MR_VIEW_H)
#define MR_LACE_CORE1 1
#else
#define MR_LACE_CORE1 0
#endif

#if MR_LACE_CORE1
static void lace_present_sync(void);
static void lace_core1_main(void);
#endif

static mr_timestep_t stress_step;
static unsigned long stress_sim_ticks;
static gfx_color_t tile_buffer_a[MR_SCREEN_W * MR_TILE_H];
#if MR_LACE_CORE1 ||                                                           \
    !(((MR_STRESS_PICO_FLUSH_MODE == 5) ||                                     \
       (MR_STRESS_PICO_FLUSH_MODE == 6) ||                                     \
       (MR_STRESS_PICO_FLUSH_MODE == 7)) &&                                    \
      (MR_TILE_H >= MR_VIEW_H))
static gfx_color_t tile_buffer_b[MR_SCREEN_W * MR_TILE_H];
#endif
#if MR_STRESS_NEEDS_FULL_FRAME
static gfx_color_t stress_prev_frame[MR_SCREEN_W * MR_VIEW_H];
#else
static gfx_color_t stress_prev_frame[1];
#endif
#if MR_STRESS_NEEDS_DIRTY_RECT
/*
 * Full-res dirty-rectangle mode must not allocate an additional pipeline
 * buffer. With MR_TILE_H=240, the current render tile plus the previous frame
 * already consume 300 KiB. Compare against the freshly rendered tile in-place,
 * then copy only flushed pixels into the previous-frame buffer.
 */
static gfx_color_t stress_dirty_rect_buffer[MR_SCREEN_W * MR_STRESS_PICO_DIRTY_RECT_MAX_H];
static int16_t stress_dirty_row_x0[MR_VIEW_H];
static int16_t stress_dirty_row_x1[MR_VIEW_H];
static const gfx_color_t *stress_dirtyrect_src;
static int stress_dirtyrect_src_x;
static int stress_dirtyrect_src_y;
static int stress_dirtyrect_src_w;
static int stress_dirtyrect_src_h;
#endif
static gfx_renderer_t renderer;
static mr_stress_test_t stress;
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

static unsigned long frame_counter;
static uint32_t start_fps_ms;
static uint32_t last_fps_ms;
static unsigned long last_fps_frame;
static int stress_present_this_frame;
static int stress_dirty_this_frame;
static unsigned long stress_flush_bytes;
static unsigned long stress_dirty_pixels;
static unsigned long stress_dirty_sent_pixels;
static unsigned long stress_dirty_spans;
static uint32_t stress_flush_us_accum;
static uint32_t stress_frame_us_accum;
static unsigned long stress_stat_window_frames;
static uint32_t stress_full_flush_begin_us;
static unsigned long stress_diag_fps10;
static unsigned long stress_diag_avg_fps10;
static unsigned long stress_diag_frame_us;
static unsigned long stress_diag_cpu_us;
static unsigned long stress_diag_flush_us;
static unsigned long stress_diag_window_kb;
static unsigned long stress_diag_last_flush_bytes;


static void stress_finish_buf(char *buf, char *dst, char *end) {
  if (dst < end) {
    *dst = '\0';
  } else if (end > buf) {
    end[-1] = '\0';
  } else if (buf) {
    *buf = '\0';
  }
}

static void stress_format_pico_line(char *buf, int bufsz) {
  char *dst;
  char *end;

  if (!buf || bufsz <= 0)
    return;
  dst = buf;
  end = buf + bufsz - 1;

  dst = mr_strbuf_str(dst, end, "PICO V");
  dst = mr_strbuf_u32(dst, end, (unsigned long)MR_VIEW_H);
  dst = mr_strbuf_str(dst, end, " T");
  dst = mr_strbuf_u32(dst, end, (unsigned long)MR_TILE_H);
  dst = mr_strbuf_str(dst, end, " C");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(clock_get_hz(clk_sys) / 1000u));
  dst = mr_strbuf_str(dst, end, " P");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(clock_get_hz(clk_peri) / 1000000u));
  dst = mr_strbuf_str(dst, end, " S");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(lcd.spi_baud_hz / 1000000u));
  dst = mr_strbuf_str(dst, end, " A");
  dst = mr_strbuf_u32(dst, end, stress_diag_avg_fps10 / 10ul);
  dst = mr_strbuf_char(dst, end, '.');
  dst = mr_strbuf_u32(dst, end, stress_diag_avg_fps10 % 10ul);
  stress_finish_buf(buf, dst, end + 1);
}

static void stress_format_lcdtest_line1(char *buf, int bufsz) {
  char *dst;
  char *end;

  if (!buf || bufsz <= 0)
    return;
  dst = buf;
  end = buf + bufsz - 1;

  dst = mr_strbuf_str(dst, end, "LCD A");
  dst = mr_strbuf_u32(dst, end, stress_diag_avg_fps10 / 10ul);
  dst = mr_strbuf_char(dst, end, '.');
  dst = mr_strbuf_u32(dst, end, stress_diag_avg_fps10 % 10ul);
  dst = mr_strbuf_str(dst, end, " C");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(clock_get_hz(clk_sys) / 1000u));
  dst = mr_strbuf_str(dst, end, " P");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(clock_get_hz(clk_peri) / 1000000u));
  dst = mr_strbuf_str(dst, end, " S");
  dst = mr_strbuf_u32(dst, end, (unsigned long)(lcd.spi_baud_hz / 1000000u));
  stress_finish_buf(buf, dst, end + 1);
}

static void stress_format_lcdtest_line2(char *buf, int bufsz) {
  char *dst;
  char *end;

  if (!buf || bufsz <= 0)
    return;
  dst = buf;
  end = buf + bufsz - 1;

  dst = mr_strbuf_str(dst, end, "US F");
  dst = mr_strbuf_u32(dst, end, stress_diag_frame_us);
  dst = mr_strbuf_str(dst, end, " FL");
  dst = mr_strbuf_u32(dst, end, stress_diag_flush_us);
  dst = mr_strbuf_str(dst, end, " KB");
  dst = mr_strbuf_u32(dst, end, stress_diag_window_kb);
  dst = mr_strbuf_str(dst, end, " SERIAL");
  dst = mr_strbuf_u32(dst, end, (unsigned long)MR_STRESS_PICO_SERIAL);
  stress_finish_buf(buf, dst, end + 1);
}

static void stress_draw_pico_diag(gfx_renderer_t *r) {
#if MR_STRESS_PICO_DIAG
  char buf[80];
  int y;

  if (!r)
    return;

  /* One compact footer line.  The older two-line footer was useful during
   * clock bring-up, but on short viewports it could be the only visible
   * artifact/glitch.  A single 8-pixel line fits cleanly at the bottom of
   * any tile-aligned viewport such as 192, 208, 224, or 240. */
  y = MR_VIEW_H - 8;
  if (y < 0)
    y = 0;

  gfx_fill_rect(r, 0, y, MR_SCREEN_W, 8, GFX_RGB565_BLACK);

  stress_format_pico_line(buf, (int)sizeof(buf));
  gfx_draw_text5x7(r, 2, y, buf, GFX_RGB565_WHITE, 1);
#else
  (void)r;
#endif
}

static void draw_stress_scene(gfx_renderer_t *r, void *user) {
  mr_stress_render(r, (mr_stress_test_t *)user);
  stress_draw_pico_diag(r);
}

static void screenshot_wait_for_display(void *user) {
  (void)user;
#if MR_LACE_CORE1
  /* Core 1 may be mid-frame and owns both the panel and the buffer the
     screenshot service wants to reuse. Let it finish first. */
  lace_present_sync();
#endif
  mr_pico_ili9341_flush_wait(&renderer, &lcd);
}

static int stress_usb_ready(void) {
#if MR_STRESS_PICO_SERIAL
  return stdio_usb_connected() ? 1 : 0;
#else
  return 0;
#endif
}

static void stress_printf(const char *fmt, ...) {
#if MR_STRESS_PICO_SERIAL
  va_list ap;

  if (!stress_usb_ready())
    return;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
#else
  (void)fmt;
#endif
}

static void stress_wait_for_usb_if_requested(void) {
#if MR_STRESS_PICO_SERIAL && MR_STRESS_PICO_WAIT_USB_MS > 0
  uint32_t start = to_ms_since_boot(get_absolute_time());
  while (!stdio_usb_connected() &&
         (uint32_t)(to_ms_since_boot(get_absolute_time()) - start) <
             (uint32_t)MR_STRESS_PICO_WAIT_USB_MS) {
    sleep_ms(10);
  }
#endif
}

static uint32_t stress_time_us(void) { return time_us_32(); }

static void stress_reset_prev_frame(gfx_color_t color) {
#if MR_STRESS_NEEDS_FULL_FRAME
  int i;
  for (i = 0; i < MR_SCREEN_W * MR_VIEW_H; ++i)
    stress_prev_frame[i] = color;
#else
  stress_prev_frame[0] = color;
#endif
}

static void stress_reset_curr_frame(gfx_color_t color) {
  (void)color;
#if MR_STRESS_NEEDS_DIRTY_RECT
  stress_dirtyrect_src = 0;
  stress_dirtyrect_src_x = 0;
  stress_dirtyrect_src_y = 0;
  stress_dirtyrect_src_w = 0;
  stress_dirtyrect_src_h = 0;
#endif
}


static void stress_null_flush(gfx_renderer_t *r, int x, int y, int w, int h,
                              const gfx_color_t *pixels, void *user) {
  (void)r;
  (void)x;
  (void)y;
  (void)pixels;
  (void)user;
  if (w > 0 && h > 0)
    stress_flush_bytes += (unsigned long)w * (unsigned long)h * 2ul;
}

static void stress_dirty_flush(gfx_renderer_t *r, int x, int y, int w, int h,
                               const gfx_color_t *pixels, void *user) {
  int row;
  int src_stride;
  uint32_t t0;

  if (!pixels || w <= 0 || h <= 0)
    return;

  src_stride = w;

  if (x < 0) {
    pixels += -x;
    w += x;
    x = 0;
  }
  if (y < 0) {
    pixels += (-y) * src_stride;
    h += y;
    y = 0;
  }
  if (x + w > MR_SCREEN_W)
    w = MR_SCREEN_W - x;
  if (y + h > MR_VIEW_H)
    h = MR_VIEW_H - y;
  if (w <= 0 || h <= 0)
    return;

  t0 = stress_time_us();

  for (row = 0; row < h; ++row) {
    const gfx_color_t *src;
    gfx_color_t *prev;
    int col;

    src = pixels + row * src_stride;
    prev = stress_prev_frame + (y + row) * MR_SCREEN_W + x;
    col = 0;

    while (col < w) {
      int start;
      int end;
      int gap;
      int k;

      while (col < w && src[col] == prev[col])
        ++col;
      if (col >= w)
        break;

      start = col;
      end = col + 1;
      gap = 0;
      ++stress_dirty_pixels;
      ++col;

      while (col < w) {
        if (src[col] != prev[col]) {
          ++stress_dirty_pixels;
          end = col + 1;
          gap = 0;
        } else {
          ++gap;
          if (gap > MR_STRESS_PICO_DIRTY_MERGE_GAP)
            break;
        }
        ++col;
      }

      for (k = start; k < end; ++k)
        prev[k] = src[k];

      mr_pico_ili9341_flush(r, x + start, y + row, end - start, 1, src + start,
                            user);
      stress_dirty_sent_pixels += (unsigned long)(end - start);
      stress_flush_bytes += (unsigned long)(end - start) * 2ul;
      ++stress_dirty_spans;
    }
  }

  stress_flush_us_accum += (uint32_t)(stress_time_us() - t0);
}


static void stress_dirtyrect_capture_tile(int x, int y, int w, int h,
                                          const gfx_color_t *pixels) {
#if MR_STRESS_NEEDS_DIRTY_RECT
  /*
   * Store only a pointer to the just-rendered full-resolution tile buffer.
   * We compare/present before the next frame overwrites it.  This removes the
   * extra 150 KiB current-frame shadow buffer that made v5 overflow RAM.
   */
  stress_dirtyrect_src = pixels;
  stress_dirtyrect_src_x = x;
  stress_dirtyrect_src_y = y;
  stress_dirtyrect_src_w = w;
  stress_dirtyrect_src_h = h;
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)pixels;
#endif
}

static int stress_dirtyrect_ranges_touch(int ax0, int ax1, int bx0, int bx1) {
  return (bx0 <= ax1 + MR_STRESS_PICO_DIRTY_MERGE_GAP &&
          ax0 <= bx1 + MR_STRESS_PICO_DIRTY_MERGE_GAP)
             ? 1
             : 0;
}

static void stress_dirtyrect_flush_rect(gfx_renderer_t *r, int x0, int y0,
                                        int x1, int y1, void *user) {
#if MR_STRESS_NEEDS_DIRTY_RECT
  int w;
  int h;
  int row;
  int col;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= MR_SCREEN_W)
    x1 = MR_SCREEN_W - 1;
  if (y1 >= MR_VIEW_H)
    y1 = MR_VIEW_H - 1;
  if (x1 < x0 || y1 < y0)
    return;

  w = x1 - x0 + 1;
  h = y1 - y0 + 1;
  if (h > MR_STRESS_PICO_DIRTY_RECT_MAX_H)
    h = MR_STRESS_PICO_DIRTY_RECT_MAX_H;

  for (row = 0; row < h; ++row) {
    const gfx_color_t *src = stress_dirtyrect_src + (y0 + row) * MR_SCREEN_W + x0;
    gfx_color_t *tmp = stress_dirty_rect_buffer + row * w;
    gfx_color_t *prev = stress_prev_frame + (y0 + row) * MR_SCREEN_W + x0;
    for (col = 0; col < w; ++col) {
      gfx_color_t c = src[col];
      tmp[col] = c;
      prev[col] = c;
    }
  }

  mr_pico_ili9341_flush(r, x0, y0, w, h, stress_dirty_rect_buffer, user);
  stress_dirty_sent_pixels += (unsigned long)w * (unsigned long)h;
  stress_flush_bytes += (unsigned long)w * (unsigned long)h * 2ul;
  ++stress_dirty_spans;
#else
  (void)r;
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  (void)user;
#endif
}

static void stress_dirtyrect_present(gfx_renderer_t *r, void *user) {
#if MR_STRESS_NEEDS_DIRTY_RECT
  int y;
  int pending;
  int rx0;
  int rx1;
  int ry0;
  int ry1;
  unsigned long candidate_pixels;
  uint32_t t0;

  if (!stress_dirtyrect_src || stress_dirtyrect_src_x != 0 ||
      stress_dirtyrect_src_y != 0 || stress_dirtyrect_src_w != MR_SCREEN_W ||
      stress_dirtyrect_src_h < MR_VIEW_H) {
    return;
  }

  t0 = stress_time_us();
  candidate_pixels = 0ul;

  for (y = 0; y < MR_VIEW_H; ++y) {
    const gfx_color_t *src = stress_dirtyrect_src + y * MR_SCREEN_W;
    const gfx_color_t *prev = stress_prev_frame + y * MR_SCREEN_W;
    int x0 = 0;
    int x1 = MR_SCREEN_W - 1;

    while (x0 < MR_SCREEN_W && src[x0] == prev[x0])
      ++x0;
    if (x0 >= MR_SCREEN_W) {
      stress_dirty_row_x0[y] = -1;
      stress_dirty_row_x1[y] = -1;
      continue;
    }
    while (x1 > x0 && src[x1] == prev[x1])
      --x1;

    stress_dirty_row_x0[y] = (int16_t)x0;
    stress_dirty_row_x1[y] = (int16_t)x1;
    candidate_pixels += (unsigned long)(x1 - x0 + 1);
    stress_dirty_pixels += (unsigned long)(x1 - x0 + 1);
  }

  if (candidate_pixels == 0ul) {
    stress_flush_us_accum += (uint32_t)(stress_time_us() - t0);
    return;
  }

  if (candidate_pixels * 100ul >=
      (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H *
          (unsigned long)MR_STRESS_PICO_DIRTY_FULL_THRESHOLD_PCT) {
    int i;
    mr_pico_ili9341_flush(r, 0, 0, MR_SCREEN_W, MR_VIEW_H,
                          stress_dirtyrect_src, user);
    for (i = 0; i < MR_SCREEN_W * MR_VIEW_H; ++i)
      stress_prev_frame[i] = stress_dirtyrect_src[i];
    stress_dirty_sent_pixels +=
        (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H;
    stress_flush_bytes +=
        (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H * 2ul;
    ++stress_dirty_spans;
    stress_flush_us_accum += (uint32_t)(stress_time_us() - t0);
    return;
  }

  pending = 0;
  rx0 = rx1 = ry0 = ry1 = 0;
  for (y = 0; y < MR_VIEW_H; ++y) {
    int x0 = stress_dirty_row_x0[y];
    int x1 = stress_dirty_row_x1[y];

    if (x0 < 0) {
      if (pending) {
        stress_dirtyrect_flush_rect(r, rx0, ry0, rx1, ry1, user);
        pending = 0;
      }
      continue;
    }

    if (!pending) {
      rx0 = x0;
      rx1 = x1;
      ry0 = y;
      ry1 = y;
      pending = 1;
      continue;
    }

    if (y == ry1 + 1 &&
        (ry1 - ry0 + 1) < MR_STRESS_PICO_DIRTY_RECT_MAX_H &&
        stress_dirtyrect_ranges_touch(rx0, rx1, x0, x1)) {
      if (x0 < rx0)
        rx0 = x0;
      if (x1 > rx1)
        rx1 = x1;
      ry1 = y;
    } else {
      stress_dirtyrect_flush_rect(r, rx0, ry0, rx1, ry1, user);
      rx0 = x0;
      rx1 = x1;
      ry0 = y;
      ry1 = y;
    }
  }

  if (pending)
    stress_dirtyrect_flush_rect(r, rx0, ry0, rx1, ry1, user);

  stress_flush_us_accum += (uint32_t)(stress_time_us() - t0);
#else
  (void)r;
  (void)user;
#endif
}

static void stress_flush_begin(gfx_renderer_t *r, int x, int y, int w, int h,
                               const gfx_color_t *pixels, void *user) {
  if (!stress_present_this_frame) {
    stress_null_flush(r, x, y, w, h, pixels, user);
    return;
  }

#if MR_STRESS_PICO_FLUSH_MODE == 5
  if (stress_dirty_this_frame) {
    if (x == 0 && y == 0 && w == MR_SCREEN_W && h >= MR_VIEW_H) {
      stress_dirtyrect_capture_tile(x, y, w, h, pixels);
    } else {
      /* Tiled dirtyrect builds fall back to immediate row-span flushing. */
      stress_dirty_flush(r, x, y, w, h, pixels, user);
    }
    return;
  }
#else
  if (stress_dirty_this_frame) {
    stress_dirty_flush(r, x, y, w, h, pixels, user);
    return;
  }
#endif

  if (w > 0 && h > 0)
    stress_flush_bytes += (unsigned long)w * (unsigned long)h * 2ul;
  stress_full_flush_begin_us = stress_time_us();
  mr_pico_ili9341_flush_begin(r, x, y, w, h, pixels, user);
}

static void stress_flush_wait(gfx_renderer_t *r, void *user) {
  if (!stress_present_this_frame) {
    (void)r;
    (void)user;
    return;
  }
  if (stress_dirty_this_frame) {
    (void)r;
    (void)user;
    return;
  }

  mr_pico_ili9341_flush_wait(r, user);
  stress_flush_us_accum +=
      (uint32_t)(stress_time_us() - stress_full_flush_begin_us);
}

static void stress_reset_timing(void) {
  frame_counter = 0;
  last_fps_frame = 0;
  stress_flush_bytes = 0ul;
  stress_dirty_pixels = 0ul;
  stress_dirty_sent_pixels = 0ul;
  stress_dirty_spans = 0ul;
  stress_flush_us_accum = 0u;
  stress_frame_us_accum = 0u;
  stress_stat_window_frames = 0ul;
  stress_diag_fps10 = 0ul;
  stress_diag_avg_fps10 = 0ul;
  stress_diag_frame_us = 0ul;
  stress_diag_cpu_us = 0ul;
  stress_diag_flush_us = 0ul;
  stress_diag_window_kb = 0ul;
  stress_diag_last_flush_bytes = stress_flush_bytes;
  start_fps_ms = to_ms_since_boot(get_absolute_time());
  last_fps_ms = start_fps_ms;
  mr_stress_set_fps10(&stress, 0ul, 0ul);
}

static void stress_draw_lcdtest_frame(gfx_renderer_t *r, unsigned long frame) {
  char buf[80];
  int x;
  int y;
  int bar;

  gfx_begin_tile(r, 0, MR_VIEW_H);

  for (y = 0; y < MR_VIEW_H; ++y) {
    gfx_color_t c;
    if (y < 80)
      c = GFX_RGB565(20 + ((int)frame & 31), 30, 80);
    else if (y < 160)
      c = GFX_RGB565(20, 80 + ((int)frame & 31), 30);
    else
      c = GFX_RGB565(80, 20, 30 + ((int)frame & 31));
    for (x = 0; x < MR_SCREEN_W; ++x)
      r->tile[y * MR_SCREEN_W + x] = c;
  }

  bar = (int)(frame % (unsigned long)(MR_SCREEN_W + 64)) - 64;
  gfx_fill_rect(r, bar, 96, 64, 48, GFX_RGB565_WHITE);
  stress_format_lcdtest_line1(buf, (int)sizeof(buf));
  gfx_draw_text5x7(r, 2, 2, buf, GFX_RGB565_WHITE, 1);
  stress_format_lcdtest_line2(buf, (int)sizeof(buf));
  gfx_draw_text5x7(r, 2, 12, buf, GFX_RGB565_WHITE, 1);
}

static void draw_lcdtest_scene(gfx_renderer_t *r, void *user) {
  (void)user;
  stress_draw_lcdtest_frame(r, frame_counter);
}

static int stress_can_fullframe_pipeline(void) {
#if MR_PICO_FRAME_PIPELINE && (MR_STRESS_PICO_FLUSH_MODE == 0)
  return (MR_TILE_H >= MR_VIEW_H) ? 1 : 0;
#else
  return 0;
#endif
}

static void stress_render_fullframe_pipelined(gfx_color_t *buffer) {
  uint32_t flush_t0;

  renderer.tile = buffer;
  gfx_begin_tile(&renderer, 0, MR_VIEW_H);
  draw_stress_scene(&renderer, &stress);

  flush_t0 = stress_time_us();
  if (lcd.dma_active)
    mr_pico_ili9341_flush_wait(&renderer, &lcd);

  stress_flush_bytes +=
      (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H * 2ul;
  mr_pico_ili9341_flush_begin(&renderer, 0, 0, MR_SCREEN_W, MR_VIEW_H, buffer,
                              &lcd);
  stress_flush_us_accum += (uint32_t)(stress_time_us() - flush_t0);
}

static void stress_render_fullframe_dirtyrect(gfx_color_t *buffer) {
#if MR_STRESS_PICO_FLUSH_MODE == 5
  renderer.tile = buffer;
  gfx_begin_tile(&renderer, 0, MR_VIEW_H);
  draw_stress_scene(&renderer, &stress);
  stress_dirtyrect_capture_tile(0, 0, MR_SCREEN_W, MR_VIEW_H, buffer);
#else
  (void)buffer;
#endif
}

#ifndef MR_STRESS_LACE_PHASES
#define MR_STRESS_LACE_PHASES 2
#endif

#if MR_STRESS_PICO_FLUSH_MODE == 6
/* Send one lace phase. Callable from either core: the renderer argument is
   unused by the ILI9341 flush, so this touches only the panel and the buffer
   handed to it. */
static void stress_lace_send_phase(const gfx_color_t *buffer, int phase) {
  int block_h = MR_STRESS_PICO_LACE_BLOCK_H;
  int y;
  int stride;

  if (block_h < 1)
    block_h = 1;
  if (block_h > MR_VIEW_H)
    block_h = MR_VIEW_H;

  /* One phase means every row every frame: no interlace, no shimmer, and one
     window setup instead of thirty. Worth it once the payload is small enough
     to fit a full frame in the time budget, which is what 12 bpp buys. */
  stride = (MR_STRESS_LACE_PHASES <= 1) ? block_h : block_h * 2;
  if (MR_STRESS_LACE_PHASES <= 1)
    phase = 0;

  for (y = phase * block_h; y < MR_VIEW_H; y += stride) {
    int h = block_h;
    if (y + h > MR_VIEW_H)
      h = MR_VIEW_H - y;
    if (h <= 0)
      continue;
    mr_pico_ili9341_flush(0, 0, y, MR_SCREEN_W, h, buffer + y * MR_SCREEN_W,
                          &lcd);
  }
}
#endif

#if MR_LACE_CORE1
static const gfx_color_t *volatile lace_present_buffer;
static volatile int lace_present_phase;
static int lace_present_pending;

static void lace_core1_main(void) {
  for (;;) {
    (void)multicore_fifo_pop_blocking();
    __dmb();
    stress_lace_send_phase((const gfx_color_t *)lace_present_buffer,
                           lace_present_phase);
    __dmb();
    multicore_fifo_push_blocking(1u);
  }
}

/* Block until core 1 has finished the frame it was given, if any. */
static void lace_present_sync(void) {
  if (!lace_present_pending)
    return;
  (void)multicore_fifo_pop_blocking();
  lace_present_pending = 0;
}

static void lace_present_async(const gfx_color_t *buffer, int phase) {
  lace_present_sync();
  lace_present_buffer = buffer;
  lace_present_phase = phase;
  __dmb();
  multicore_fifo_push_blocking(1u);
  lace_present_pending = 1;
}

static void stress_render_fullframe_lace_core1(void) {
  /* Core 1 is reading the buffer core 0 filled last frame, so core 0 must
     render into the other one. */
  gfx_color_t *buffer = (frame_counter & 1ul) ? tile_buffer_b : tile_buffer_a;
  uint32_t flush_t0;

  renderer.tile = buffer;
  gfx_begin_tile(&renderer, 0, MR_VIEW_H);
  draw_stress_scene(&renderer, &stress);

  /* Whatever is left of the previous frame's transfer after this frame's
     render is the only part that still stalls core 0. When rasterization is
     the shorter of the two this converges on pure transfer time. */
  flush_t0 = stress_time_us();
  lace_present_sync();
  stress_flush_us_accum += (uint32_t)(stress_time_us() - flush_t0);

  /* sentKB must reflect what actually went down the wire, or the throughput
     line silently lies once either the phase count or the pixel format
     changes. */
  {
    unsigned long rows =
        (MR_STRESS_LACE_PHASES <= 1) ? (unsigned long)MR_VIEW_H
                                     : ((unsigned long)MR_VIEW_H / 2ul);
    stress_flush_bytes += (unsigned long)MR_SCREEN_W * rows * 2ul;
  }
  lace_present_async(buffer, (int)(frame_counter & 1ul));
}
#endif

static void stress_render_fullframe_lace(gfx_color_t *buffer) {
#if MR_STRESS_PICO_FLUSH_MODE == 6
  uint32_t flush_t0;

  renderer.tile = buffer;
  gfx_begin_tile(&renderer, 0, MR_VIEW_H);
  draw_stress_scene(&renderer, &stress);

  flush_t0 = stress_time_us();
  stress_lace_send_phase(buffer, (int)(frame_counter & 1ul));
  stress_flush_us_accum += (uint32_t)(stress_time_us() - flush_t0);
  stress_flush_bytes += (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H;
#else
  (void)buffer;
#endif
}

static void stress_lcdtest_loop(void) {
  uint32_t now;

  gfx_init(&renderer, MR_SCREEN_W, MR_VIEW_H, stress_prev_frame, MR_VIEW_H,
           mr_pico_ili9341_flush, &lcd);
  mr_pico_screenshot_init(&screenshot_service, MR_SCREEN_W, MR_VIEW_H,
                          stress_prev_frame, MR_VIEW_H, GFX_RGB565_BLACK,
                          draw_lcdtest_scene, 0, screenshot_wait_for_display,
                          0);

  mr_timestep_init(&stress_step, 60, 5);
  stress_reset_timing();
  for (;;) {
    uint32_t frame_t0;
    uint32_t flush_t0;

    frame_t0 = stress_time_us();
    stress_draw_lcdtest_frame(&renderer, frame_counter);
    flush_t0 = stress_time_us();
    gfx_flush_tile(&renderer);
    stress_flush_us_accum += (uint32_t)(stress_time_us() - flush_t0);
    stress_flush_bytes +=
        (unsigned long)MR_SCREEN_W * (unsigned long)MR_VIEW_H * 2ul;
    stress_frame_us_accum += (uint32_t)(stress_time_us() - frame_t0);
    ++stress_stat_window_frames;
    ++frame_counter;
    (void)mr_pico_screenshot_poll(&screenshot_service);

    now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_fps_ms) >= MR_STRESS_PICO_PRINT_MS) {
      unsigned long frames;
      unsigned long fps10;
      unsigned long avg_fps10;
      unsigned long frame_us;
      unsigned long flush_us;
      uint32_t delta_ms;
      uint32_t total_ms;
      unsigned long window_bytes;

      frames = frame_counter - last_fps_frame;
      delta_ms = (uint32_t)(now - last_fps_ms);
      total_ms = (uint32_t)(now - start_fps_ms);
      fps10 = delta_ms ? (frames * 10000ul) / (unsigned long)delta_ms : 0ul;
      avg_fps10 =
          total_ms ? (frame_counter * 10000ul) / (unsigned long)total_ms : 0ul;
      frame_us = stress_stat_window_frames
                     ? (unsigned long)(stress_frame_us_accum /
                                       (uint32_t)stress_stat_window_frames)
                     : 0ul;
      flush_us = stress_stat_window_frames
                     ? (unsigned long)(stress_flush_us_accum /
                                       (uint32_t)stress_stat_window_frames)
                     : 0ul;
      window_bytes = stress_flush_bytes - stress_diag_last_flush_bytes;

      stress_diag_fps10 = fps10;
      stress_diag_avg_fps10 = avg_fps10;
      stress_diag_frame_us = frame_us;
      stress_diag_cpu_us = (frame_us > flush_us) ? (frame_us - flush_us) : 0ul;
      stress_diag_flush_us = flush_us;
      stress_diag_window_kb = window_bytes / 1024ul;
      stress_diag_last_flush_bytes = stress_flush_bytes;

      stress_printf(
          "lcdtest frame=%lu fps=%lu.%lu avg=%lu.%lu frameUs=%lu flushUs=%lu "
          "sentKB=%lu sys=%lu peri=%lu spi=%u serial=%d\n",
          frame_counter, fps10 / 10ul, fps10 % 10ul, avg_fps10 / 10ul,
          avg_fps10 % 10ul, frame_us, flush_us, stress_flush_bytes / 1024ul,
          (unsigned long)clock_get_hz(clk_sys),
          (unsigned long)clock_get_hz(clk_peri), (unsigned)lcd.spi_baud_hz,
          MR_STRESS_PICO_SERIAL);

      last_fps_frame = frame_counter;
      last_fps_ms = now;
      stress_frame_us_accum = 0u;
      stress_flush_us_accum = 0u;
      stress_stat_window_frames = 0ul;
    }
  }
}

static void stress_configure_peripheral_clock(void) {
#if MR_PICO_PERI_FROM_SYS
  uint32_t sys_hz = clock_get_hz(clk_sys);
  if (sys_hz > 0) {
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    sys_hz, sys_hz);
    sleep_ms(2);
  }
#endif
}

static int stress_configure_split_pll_clock(void) {
#if MR_PICO_PERI_PLL_KHZ > 0u
  uint32_t pll_hz = (uint32_t)MR_PICO_PERI_PLL_KHZ * 1000u;
  uint32_t sys_hz = (uint32_t)MR_PICO_SYS_KHZ * 1000u;

  if (pll_hz == 0u || sys_hz == 0u)
    return 0;

  {
    uint vco_freq;
    uint post_div1;
    uint post_div2;
    uint32_t ref_hz;

    if (pll_hz < sys_hz)
      return 0;

    /* Find PLL settings for the *peripheral* rate without applying them.
       set_sys_clock_khz() would apply them to clk_sys, which is the bug this
       replaces: asking for a 340 MHz peripheral PLL used to run the whole
       chip -- CPU and XIP flash reads -- at 340 MHz before dividing back
       down, while executing the very code doing the dividing. On a Pico
       Plus 2 that reliably white-screens. */
    if (!check_sys_clock_khz((uint32_t)MR_PICO_PERI_PLL_KHZ, &vco_freq,
                             &post_div1, &post_div2))
      return 0;

    ref_hz = clock_get_hz(clk_ref);

    /* Park clk_sys and clk_peri on the reference so pll_sys can be retuned
       with nothing downstream of it running. clk_ref is slower than the
       current clk_sys, never faster, so flash stays readable throughout. */
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, ref_hz,
                    ref_hz);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    ref_hz, ref_hz);

    pll_init(pll_sys, 1, vco_freq, post_div1, post_div2);

    /* clk_sys comes back at the requested system rate, divided down from the
       PLL. It never runs above MR_PICO_SYS_KHZ at any point. */
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, pll_hz,
                    sys_hz);

    /* clk_peri has no divider, so it takes the full PLL rate. That is the
       whole point: it is what lets the SPI divider reach rates clk_sys
       cannot produce. */
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, pll_hz,
                    pll_hz);
    sleep_ms(2);
    return 1;
  }
#else
  return 0;
#endif
}

static void stress_clear_lcd(gfx_color_t color) {
  static gfx_color_t line[MR_SCREEN_W];
  int x;
  int y;

  for (x = 0; x < MR_SCREEN_W; ++x)
    line[x] = color;
  for (y = 0; y < MR_SCREEN_H; ++y)
    mr_pico_ili9341_flush(0, 0, y, MR_SCREEN_W, 1, line, &lcd);
}

/* Recover from a warm boot.
 *
 * picotool's "load -x", the USB reset interface, and any watchdog reboot
 * restart the CPU without power-cycling anything else. The previous image's
 * DMA channels can still be running, its core 1 can still be executing, and
 * both of those touch the same SPI peripheral this image is about to
 * configure. Dragging a UF2 onto the board in BOOTSEL is a genuine cold start
 * and does not have the problem, which is why an automatic flash could come up
 * with a white display where a manual one did not.
 *
 * Aborting every DMA channel and parking core 1 before any peripheral setup
 * costs microseconds on a cold boot, where there is nothing to abort, and
 * makes a warm boot behave like a cold one.
 */
static void stress_recover_from_warm_boot(void) {
#if MR_LACE_CORE1
  /* Safe when core 1 was never started: the reset is unconditional. */
  multicore_reset_core1();
#endif

  /* Abort every channel, then wait for the aborts to retire. A channel left
     mid-transfer would otherwise keep feeding the SPI FIFO underneath the
     panel initialisation sequence. */
  dma_hw->abort = (uint32_t)~0u;
  while (dma_hw->abort)
    tight_loop_contents();
}

void mr_pico_stress_demo_main(void) {
  mr_stress_config_t cfg;
  uint32_t now;
  int clock_ok;

  stress_recover_from_warm_boot();

  stress_wait_for_usb_if_requested();

  clock_ok = 1;
#if MR_PICO_PERI_PLL_KHZ > 0u
  clock_ok = stress_configure_split_pll_clock();
#else
#if MR_PICO_SYS_KHZ > 0
  clock_ok = set_sys_clock_khz((uint32_t)MR_PICO_SYS_KHZ,
                               MR_PICO_CLOCK_REQUIRED ? true : false)
                 ? 1
                 : 0;
  sleep_ms(10);
#endif
  stress_configure_peripheral_clock();
#endif

  stress_printf("MicroRender RP2350 RLE/collision stress demo\n");
  stress_printf("pins: MISO=%u CS=%u SCK=%u MOSI=%u RST=%u DC=%u\n",
                (unsigned)MR_LCD_PIN_MISO, (unsigned)MR_LCD_PIN_CS,
                (unsigned)MR_LCD_PIN_SCK, (unsigned)MR_LCD_PIN_MOSI,
                (unsigned)MR_LCD_PIN_RST, (unsigned)MR_LCD_PIN_DC);
  stress_printf(
      "screen: %dx%d view_h=%d tile_h=%d sprites=%d target=%d flush_mode=%d "
      "every=%d statsrate=%d fixedcam=%d tris=%d mergegap=%d serial=%d "
      "peri_from_sys=%d peri_pll_khz=%u diag=%d framepipe=%d\n",
      MR_SCREEN_W, MR_SCREEN_H, MR_VIEW_H, MR_TILE_H, MR_STRESS_SPRITES,
      MR_STRESS_TARGET_FPS, MR_STRESS_PICO_FLUSH_MODE, MR_STRESS_PICO_LCD_EVERY,
      MR_STRESS_STATS_RATE, MR_STRESS_FIXED_CAMERA, MR_STRESS_ENABLE_TRIANGLES,
      MR_STRESS_PICO_DIRTY_MERGE_GAP, MR_STRESS_PICO_SERIAL,
      MR_PICO_PERI_FROM_SYS, (unsigned)MR_PICO_PERI_PLL_KHZ,
      MR_STRESS_PICO_DIAG, MR_PICO_FRAME_PIPELINE);
  stress_printf("clock: requested_sys=%u kHz actual_sys=%lu Hz peri=%lu Hz "
                "clock_ok=%d requested_spi=%u Hz\n",
                (unsigned)MR_PICO_SYS_KHZ, (unsigned long)clock_get_hz(clk_sys),
                (unsigned long)clock_get_hz(clk_peri), clock_ok,
                (unsigned)MR_LCD_SPI_BAUD);

  mr_pico_ili9341_init(&lcd);
  stress_printf("spi: requested=%u Hz actual=%u Hz\n",
                (unsigned)MR_LCD_SPI_BAUD, (unsigned)lcd.spi_baud_hz);
  mr_pico_ili9341_panel_init(&lcd);
  stress_clear_lcd(GFX_RGB565_BLACK);
  stress_reset_prev_frame(GFX_RGB565_BLACK);
  stress_reset_curr_frame(GFX_RGB565_BLACK);

#if MR_STRESS_PICO_FLUSH_MODE == 4
  stress_lcdtest_loop();
#endif

  gfx_init(&renderer, MR_SCREEN_W, MR_VIEW_H, tile_buffer_a, MR_TILE_H,
           mr_pico_ili9341_flush, &lcd);
  gfx_set_async_flush(&renderer, stress_flush_begin, stress_flush_wait);

  mr_stress_config_defaults(&cfg, MR_SCREEN_W, MR_VIEW_H);
  cfg.sprite_count = MR_STRESS_SPRITES;
  cfg.target_fps = MR_STRESS_TARGET_FPS;
  cfg.stats_sample_rate = MR_STRESS_STATS_RATE;
  cfg.features = MR_STRESS_FEATURE_DEFAULT;
#if !MR_STRESS_ENABLE_TRIANGLES
  cfg.features &= ~MR_STRESS_FEATURE_TRIANGLES;
#endif
  mr_stress_init(&stress, &cfg);
  mr_pico_screenshot_init(&screenshot_service, MR_SCREEN_W, MR_VIEW_H,
                          tile_buffer_a, MR_TILE_H, GFX_RGB565_BLACK,
                          draw_stress_scene, &stress,
                          screenshot_wait_for_display, 0);

  stress_present_this_frame = 1;
  stress_dirty_this_frame = 0;

#if MR_LACE_CORE1
  /* Launch after the panel is initialised and before the first frame, so core
     1 never touches the LCD while core 0 is still configuring it. */
  multicore_launch_core1(lace_core1_main);
#endif

  /* Must be initialised on this path too, not only in the lcdtest loop. An
     unset accumulator has step_us = 0 and max_steps = 0, so advance() returns
     zero forever: the renderer keeps drawing at full speed while the scene
     never changes. That looks exactly like a hang. */
  mr_timestep_init(&stress_step, 60, 5);
  stress_reset_timing();

#if MR_STRESS_PICO_FLUSH_MODE == 1
  /*
   * Render-only mode: show exactly one visible proof frame, then reset timing
   * and profile the renderer with null flushes.  The LCD will then stay on the
   * proof frame, so USB serial is the source of truth.
   */
  stress_present_this_frame = 1;
  stress_dirty_this_frame = 0;
  {
    int steps = mr_timestep_advance(&stress_step,
                                    (unsigned long)stress_time_us());
    while (steps-- > 0) {
      mr_stress_tick(&stress);
      ++stress_sim_ticks;
    }
  }
  gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_stress_scene,
                             &stress, GFX_RGB565_BLACK, 0u);
  stress_printf("render-only proof frame shown; switching to null LCD flush, "
                "read USB serial for FPS\n");
  sleep_ms(750);
  stress_reset_timing();
#endif

  for (;;) {
    mr_stress_metrics_t m;
    uint32_t frame_t0;

#if MR_STRESS_PICO_FLUSH_MODE == 1
    stress_present_this_frame = 0;
    stress_dirty_this_frame = 0;
#elif MR_STRESS_PICO_FLUSH_MODE == 2
    stress_present_this_frame =
        (MR_STRESS_PICO_LCD_EVERY <= 1)
            ? 1
            : (((frame_counter % (unsigned long)MR_STRESS_PICO_LCD_EVERY) ==
                0ul)
                   ? 1
                   : 0);
    stress_dirty_this_frame = 0;
#elif (MR_STRESS_PICO_FLUSH_MODE == 3) || (MR_STRESS_PICO_FLUSH_MODE == 5)
    stress_present_this_frame = 1;
    stress_dirty_this_frame = 1;
#else
    stress_present_this_frame = 1;
    stress_dirty_this_frame = 0;
#endif

    frame_t0 = stress_time_us();
#if MR_STRESS_PICO_FLUSH_MODE == 5
    stress_dirtyrect_src = 0;
#endif
    {
      int steps = mr_timestep_advance(&stress_step,
                                      (unsigned long)stress_time_us());
      while (steps-- > 0) {
        mr_stress_tick(&stress);
        ++stress_sim_ticks;
      }
    }
#if MR_STRESS_PICO_FLUSH_MODE == 7
    /* Deliberately unoptimized reference path: render, synchronously flush,
       then loop. No DMA/raster overlap. */
    gfx_render_tiled(&renderer, draw_stress_scene, &stress, GFX_RGB565_BLACK);
#elif MR_LACE_CORE1
    stress_render_fullframe_lace_core1();
#elif (MR_STRESS_PICO_FLUSH_MODE == 6) && (MR_TILE_H >= MR_VIEW_H)
    stress_render_fullframe_lace(tile_buffer_a);
#elif (MR_STRESS_PICO_FLUSH_MODE == 5) && (MR_TILE_H >= MR_VIEW_H)
    stress_render_fullframe_dirtyrect(tile_buffer_a);
#else
    if (stress_can_fullframe_pipeline()) {
      stress_render_fullframe_pipelined((frame_counter & 1ul) ? tile_buffer_b
                                                              : tile_buffer_a);
    } else {
      gfx_render_tiled_pipelined(&renderer, tile_buffer_b, draw_stress_scene,
                                 &stress, GFX_RGB565_BLACK, 0u);
    }
#endif
#if MR_STRESS_PICO_FLUSH_MODE == 5
    if (stress_present_this_frame && stress_dirty_this_frame)
      stress_dirtyrect_present(&renderer, &lcd);
#endif
    stress_frame_us_accum += (uint32_t)(stress_time_us() - frame_t0);
    ++stress_stat_window_frames;
    ++frame_counter;
    (void)mr_pico_screenshot_poll(&screenshot_service);

    now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - last_fps_ms) >= MR_STRESS_PICO_PRINT_MS) {
      unsigned long frames;
      unsigned long fps10;
      unsigned long avg_fps10;
      unsigned long frame_us;
      unsigned long flush_us;
      unsigned long cpu_us;
      unsigned long dirty_pct10;
      uint32_t delta_ms;
      uint32_t total_ms;
      unsigned long sim_hz10;

      frames = frame_counter - last_fps_frame;
      delta_ms = (uint32_t)(now - last_fps_ms);
      total_ms = (uint32_t)(now - start_fps_ms);
      fps10 = delta_ms ? (frames * 10000ul) / (unsigned long)delta_ms : 0ul;
      avg_fps10 =
          total_ms ? (frame_counter * 10000ul) / (unsigned long)total_ms : 0ul;
      /* Simulation rate, computed exactly like the frame rate so the two are
         directly comparable. This is the number that should agree across DOS,
         Raylib and Pico; the frame rate is expected not to. */
      sim_hz10 = total_ms
                     ? (stress_sim_ticks * 10000ul) / (unsigned long)total_ms
                     : 0ul;
      mr_stress_set_fps10(&stress, fps10, avg_fps10);

      frame_us = stress_stat_window_frames
                     ? (unsigned long)(stress_frame_us_accum /
                                       (uint32_t)stress_stat_window_frames)
                     : 0ul;
      flush_us = stress_stat_window_frames
                     ? (unsigned long)(stress_flush_us_accum /
                                       (uint32_t)stress_stat_window_frames)
                     : 0ul;
      cpu_us = (frame_us > flush_us) ? (frame_us - flush_us) : 0ul;
      dirty_pct10 = (frame_counter > 0ul)
                        ? (stress_dirty_sent_pixels * 1000ul) /
                              ((unsigned long)MR_SCREEN_W *
                               (unsigned long)MR_VIEW_H * frame_counter)
                        : 0ul;

      stress_diag_fps10 = fps10;
      stress_diag_avg_fps10 = avg_fps10;
      stress_diag_frame_us = frame_us;
      stress_diag_cpu_us = cpu_us;
      stress_diag_flush_us = flush_us;
      stress_diag_window_kb =
          (stress_flush_bytes - stress_diag_last_flush_bytes) / 1024ul;
      stress_diag_last_flush_bytes = stress_flush_bytes;

      mr_stress_get_metrics(&stress, &m);
      stress_printf(
          "stress frame=%lu fps=%lu.%lu avg=%lu.%lu spr=%lu vis=%lu b=%lu "
          "d=%lu rn=%lu px=%lu col=%lu/%lu mode=%d fixed=%d tri=%d frameUs=%lu "
          "cpuUs=%lu flushUs=%lu sentKB=%lu dirty=%lu.%lu%% spans=%lu sys=%lu "
          "peri=%lu spi=%u serial=%d core1=%d lace=%d phases=%d "
          "sim_ticks=%lu sim_hz=%lu.%lu\n",
          frame_counter, fps10 / 10ul, fps10 % 10ul, avg_fps10 / 10ul,
          avg_fps10 % 10ul, m.sprite_count, m.sprites_visible, m.bucket_items,
          m.sprites_drawn, m.rle_runs_drawn, m.rle_pixels_copied,
          m.collision_hits, m.collision_checks, MR_STRESS_PICO_FLUSH_MODE,
          MR_STRESS_FIXED_CAMERA, MR_STRESS_ENABLE_TRIANGLES, frame_us, cpu_us,
          flush_us, stress_flush_bytes / 1024ul, dirty_pct10 / 10ul,
          dirty_pct10 % 10ul, stress_dirty_spans,
          (unsigned long)clock_get_hz(clk_sys),
          (unsigned long)clock_get_hz(clk_peri), (unsigned)lcd.spi_baud_hz,
          MR_STRESS_PICO_SERIAL, MR_LACE_CORE1,
          MR_STRESS_PICO_LACE_BLOCK_H, MR_STRESS_LACE_PHASES,
          stress_sim_ticks, sim_hz10 / 10ul, sim_hz10 % 10ul);

      last_fps_frame = frame_counter;
      last_fps_ms = now;
      stress_frame_us_accum = 0u;
      stress_flush_us_accum = 0u;
      stress_stat_window_frames = 0ul;
    }
  }
}
