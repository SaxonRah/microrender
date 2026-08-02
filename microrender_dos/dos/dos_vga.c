#include "dos_vga.h"

#ifndef __WATCOMC__
#error dos_vga.c targets Open Watcom 16-bit real mode DOS.
#endif

#if !defined(GFX_COLOR_INDEX8) || GFX_COLOR_INDEX8 != 1
#error dos_vga.c expects GFX_COLOR_INDEX8=1 (one byte per pixel in mode 13h).
#endif

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>

static unsigned char __far *dos_vga_base = (unsigned char __far *)0xA0000000L;

static volatile unsigned long __far *dos_vga_bios_ticks =
    (volatile unsigned long __far *)MK_FP(0x40, 0x6c);

static int dos_vga_mode_active = 0;
static int dos_vga_flush_on = 1;

void dos_vga_set_mode(unsigned char mode) {
  union REGS regs;
  regs.w.ax = (unsigned short)mode;
  int86(0x10, &regs, &regs);
}

void dos_vga_enter(void) {
  if (dos_vga_mode_active)
    return;
  dos_vga_set_mode(DOS_VGA_MODE_13H);
  dos_vga_mode_active = 1;
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

  /* The VGA DAC takes 6-bit components. RGB332 gives 3 bits of red and green
     and 2 of blue, so each level is scaled to span the full 0..63 range:
     7 * 9 = 63 for the 3-bit channels and 3 * 21 = 63 for the 2-bit one. */
  outp(0x3C8, 0);
  for (i = 0; i < 256; ++i) {
    int r = ((i >> 5) & 7) * 9;
    int g = ((i >> 2) & 7) * 9;
    int b = (i & 3) * 21;
    outp(0x3C9, r);
    outp(0x3C9, g);
    outp(0x3C9, b);
  }
}

void dos_vga_wait_vblank(void) {
  /* Wait out any blanking interval already in progress before waiting for the
     next one to start, so a caller that arrives mid-blank does not return
     immediately and tear on the same frame. */
  while ((inp(0x3DA) & 0x08) != 0) {
  }
  while ((inp(0x3DA) & 0x08) == 0) {
  }
}

unsigned long dos_vga_ticks(void) { return *dos_vga_bios_ticks; }

void dos_vga_set_flush_enabled(int enabled) { dos_vga_flush_on = enabled; }

int dos_vga_flush_enabled(void) { return dos_vga_flush_on; }

void dos_vga_flush_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                        const gfx_color_t GFX_PTR *pixels,
                        void GFX_PTR *user) {
  int row;
  int stride;

  (void)user;

  if (!dos_vga_flush_on || !pixels || w <= 0 || h <= 0)
    return;

  if (x < 0 || y < 0 || x + w > DOS_VGA_WIDTH || y + h > DOS_VGA_HEIGHT)
    return;

  /* Source rows are spaced by the renderer's tile stride, not by the flush
     width. Those are equal only while every tile spans the full screen; a
     sub-region or dirty-rect pass makes them differ. */
  stride = r ? r->tile_stride : w;
  if (stride < w)
    stride = w;

  for (row = 0; row < h; ++row) {
    unsigned char __far *dst =
        dos_vga_base + (unsigned long)(y + row) * (unsigned long)DOS_VGA_WIDTH +
        (unsigned long)x;
    const void GFX_PTR *src = pixels + (long)row * (long)stride;
    _fmemcpy(dst, src, (size_t)w);
  }
}
