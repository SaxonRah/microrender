#include "dos_vga.h"

#ifndef __WATCOMC__
#error dos_vga.c targets Open Watcom 16-bit real mode DOS.
#endif

#if GFX_COLOR_FORMAT != GFX_COLOR_FORMAT_RGB565
#error DOS now renders a 320x240 RGB565 logical framebuffer.
#endif

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>

#define VGA_SEQ_INDEX 0x3C4
#define VGA_CRTC_INDEX 0x3D4
#define VGA_MISC_WRITE 0x3C2
#define VGA_STATUS 0x3DA

static unsigned char __far *dos_vga_base = (unsigned char __far *)0xA0000000L;
static volatile unsigned long __far *dos_vga_bios_ticks =
    (volatile unsigned long __far *)MK_FP(0x40, 0x6c);
static int dos_vga_mode_active = 0;
static int dos_vga_flush_on = 1;
static unsigned char dos_vga_rgb565_high[256];
static int dos_vga_rgb565_table_ready = 0;

static void dos_vga_seq(unsigned char index, unsigned char value) {
  outp(VGA_SEQ_INDEX, index);
  outp(VGA_SEQ_INDEX + 1, value);
}

static void dos_vga_crtc(unsigned char index, unsigned char value) {
  outp(VGA_CRTC_INDEX, index);
  outp(VGA_CRTC_INDEX + 1, value);
}

static void dos_vga_set_write_plane(int plane) {
  dos_vga_seq(0x02u, (unsigned char)(1u << plane));
}

static void dos_vga_init_rgb565_table(void) {
  int i;
  if (dos_vga_rgb565_table_ready)
    return;
  for (i = 0; i < 256; ++i) {
    dos_vga_rgb565_high[i] =
        (unsigned char)(((i >> 5) << 5) | ((i & 0x07) << 2));
  }
  dos_vga_rgb565_table_ready = 1;
}

static unsigned char dos_vga_rgb565_to_rgb332(gfx_color_t c) {
  return (unsigned char)(dos_vga_rgb565_high[(unsigned char)(c >> 8)] |
                         ((unsigned char)c >> 3 & 0x03u));
}

void dos_vga_set_mode(unsigned char mode) {
  union REGS regs;
  regs.w.ax = (unsigned short)mode;
  int86(0x10, &regs, &regs);
}

static void dos_vga_enter_modex_320x240(void) {
  unsigned char unlock;

  /* Begin from the BIOS 320x200 mode, then unchain the four VGA planes and
     program the standard 320x240 timing. The logical scanline is 80 bytes per
     plane; four adjacent pixels occupy the same byte offset in four planes. */
  dos_vga_set_mode(DOS_VGA_MODE_13H);

  dos_vga_seq(0x00u, 0x01u); /* synchronous reset */
  outp(VGA_MISC_WRITE, 0xE3u);
  dos_vga_seq(0x04u, 0x06u); /* disable chain-4, sequential planes */
  dos_vga_seq(0x00u, 0x03u); /* restart sequencer */

  outp(VGA_CRTC_INDEX, 0x11u);
  unlock = (unsigned char)inp(VGA_CRTC_INDEX + 1);
  dos_vga_crtc(0x11u, (unsigned char)(unlock & 0x7Fu));

  dos_vga_crtc(0x06u, 0x0Du);
  dos_vga_crtc(0x07u, 0x3Eu);
  dos_vga_crtc(0x09u, 0x41u);
  dos_vga_crtc(0x10u, 0xEAu);
  dos_vga_crtc(0x11u, 0xACu);
  dos_vga_crtc(0x12u, 0xDFu);
  dos_vga_crtc(0x14u, 0x00u);
  dos_vga_crtc(0x15u, 0xE7u);
  dos_vga_crtc(0x16u, 0x06u);
  dos_vga_crtc(0x17u, 0xE3u);
  dos_vga_crtc(0x13u, 0x28u);

  dos_vga_seq(0x02u, 0x0Fu);
  _fmemset(dos_vga_base, 0, 65535u);
}

void dos_vga_enter(void) {
  if (dos_vga_mode_active)
    return;
  dos_vga_enter_modex_320x240();
  dos_vga_mode_active = 1;
  dos_vga_init_rgb565_table();
  dos_vga_set_rgb332_palette();
}

void dos_vga_leave(void) {
  if (!dos_vga_mode_active)
    return;
  dos_vga_mode_active = 0;
  dos_vga_set_mode(DOS_VGA_MODE_TEXT);
}

int dos_vga_active(void) { return dos_vga_mode_active; }

void dos_vga_set_rgb332_palette(void) {
  int i;
  outp(0x3C8, 0);
  for (i = 0; i < 256; ++i) {
    outp(0x3C9, ((i >> 5) & 7) * 9);
    outp(0x3C9, ((i >> 2) & 7) * 9);
    outp(0x3C9, (i & 3) * 21);
  }
}

void dos_vga_wait_vblank(void) {
  while ((inp(VGA_STATUS) & 0x08) != 0) {
  }
  while ((inp(VGA_STATUS) & 0x08) == 0) {
  }
}

unsigned long dos_vga_ticks(void) { return *dos_vga_bios_ticks; }
void dos_vga_set_flush_enabled(int enabled) { dos_vga_flush_on = enabled; }
int dos_vga_flush_enabled(void) { return dos_vga_flush_on; }

void dos_vga_flush_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                        const gfx_color_t GFX_PTR *pixels,
                        void GFX_PTR *user) {
  int plane;
  int row;
  int stride;

  (void)user;
  if (!dos_vga_flush_on || !pixels || w <= 0 || h <= 0)
    return;
  if (x < 0 || y < 0 || x + w > DOS_VGA_WIDTH || y + h > DOS_VGA_HEIGHT)
    return;

  stride = r ? r->tile_stride : w;
  if (stride < w)
    stride = w;

  /* Group writes by VGA plane so each tile needs only four map-mask changes,
     not one register update per pixel. RGB565-to-RGB332 conversion happens in
     the sequential inner loop while the destination pointer advances linearly. */
  for (plane = 0; plane < 4; ++plane) {
    dos_vga_set_write_plane(plane);
    for (row = 0; row < h; ++row) {
      int first;
      int px;
      const gfx_color_t GFX_PTR *src;
      unsigned char __far *dst;

      first = x + ((plane - (x & 3) + 4) & 3);
      if (first >= x + w)
        continue;
      src = pixels + (long)row * (long)stride + (first - x);
      dst = dos_vga_base +
            (unsigned long)(y + row) * DOS_VGA_BYTES_PER_PLANE_ROW +
            (unsigned long)(first >> 2);
      for (px = first; px < x + w; px += 4) {
        *dst++ = dos_vga_rgb565_to_rgb332(*src);
        src += 4;
      }
    }
  }
}


void dos_vga_present_rgb565_frame(
    const gfx_color_t DOS_VGA_HUGE *pixels) {
  int plane;
  int row;

  if (!dos_vga_flush_on || !pixels)
    return;

  /* Deliberately serialized raw path: the caller has already completed the
     whole logical frame. Convert and upload it only now. Plane grouping keeps
     VGA register traffic to four map-mask changes per frame. */
  for (plane = 0; plane < 4; ++plane) {
    dos_vga_set_write_plane(plane);
    for (row = 0; row < DOS_VGA_HEIGHT; ++row) {
      const gfx_color_t DOS_VGA_HUGE *src;
      unsigned char __far *dst;
      int x;
      src = pixels + (long)row * DOS_VGA_WIDTH + plane;
      dst = dos_vga_base +
            (unsigned long)row * DOS_VGA_BYTES_PER_PLANE_ROW;
      for (x = plane; x < DOS_VGA_WIDTH; x += 4) {
        *dst++ = dos_vga_rgb565_to_rgb332(*src);
        src += 4;
      }
    }
  }
}
