#ifndef MR_PICO_ILI9341_H
#define MR_PICO_ILI9341_H

#include "gfx.h"
#include "hardware/dma.h"
#include "hardware/spi.h"

#ifndef MR_LCD_SPI
#define MR_LCD_SPI spi0
#endif

#ifndef MR_LCD_PIN_MISO
#define MR_LCD_PIN_MISO 4
#endif
#ifndef MR_LCD_PIN_CS
#define MR_LCD_PIN_CS 5
#endif
#ifndef MR_LCD_PIN_SCK
#define MR_LCD_PIN_SCK 6
#endif
#ifndef MR_LCD_PIN_MOSI
#define MR_LCD_PIN_MOSI 7
#endif
#ifndef MR_LCD_PIN_RST
#define MR_LCD_PIN_RST 8
#endif
#ifndef MR_LCD_PIN_DC
#define MR_LCD_PIN_DC 9
#endif
#ifndef MR_LCD_SPI_BAUD
#define MR_LCD_SPI_BAUD 40000000u
#endif

#ifndef MR_ILI9341_MADCTL
#define MR_ILI9341_MADCTL 0x48u
#endif

typedef struct mr_pico_ili9341 {
  spi_inst_t *spi;
  unsigned int dma_chan;
  unsigned int pin_miso;
  unsigned int pin_cs;
  unsigned int pin_sck;
  unsigned int pin_mosi;
  unsigned int pin_rst;
  unsigned int pin_dc;
  unsigned int spi_baud_hz;
  int x_offset;
  int y_offset;
  unsigned int dma_active;
  unsigned int spi_format_bits;
  dma_channel_config dma_cfg16;
} mr_pico_ili9341_t;

void mr_pico_ili9341_init(mr_pico_ili9341_t *ctx);
void mr_pico_ili9341_panel_init(mr_pico_ili9341_t *ctx);
void mr_pico_ili9341_fill_screen(mr_pico_ili9341_t *ctx, gfx_color_t color,
                                 int w, int h);

void mr_pico_ili9341_flush(gfx_renderer_t *r, int x, int y, int w, int h,
                           const gfx_color_t *pixels, void *user);

void mr_pico_ili9341_flush_begin(gfx_renderer_t *r, int x, int y, int w, int h,
                                 const gfx_color_t *pixels, void *user);

void mr_pico_ili9341_flush_wait(gfx_renderer_t *r, void *user);

#endif
