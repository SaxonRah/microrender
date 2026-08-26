#ifndef DOS_VGA_H
#define DOS_VGA_H

#include "gfx.h"

/* 320x240 unchained VGA (Mode X) presentation for the 16-bit DOS target.

   The shared renderer remains RGB565 on every platform. Standard VGA is an
   8-bit palettized device, so the DOS flush converts each RGB565 pixel to the
   fixed RGB332 DAC palette while copying it into the four planar VGA banks.
   This preserves one 320x240 RGB565 game/renderer target without pretending
   that a stock VGA DAC can physically display 65,536 simultaneous colours.
*/

#define DOS_VGA_MODE_13H 0x13u
#define DOS_VGA_MODE_TEXT 0x03u
#define DOS_VGA_WIDTH 320
#define DOS_VGA_HEIGHT 240
#define DOS_VGA_BYTES_PER_PLANE_ROW (DOS_VGA_WIDTH / 4)

#ifdef __WATCOMC__
#define DOS_VGA_HUGE __huge
#else
#define DOS_VGA_HUGE
#endif

void dos_vga_set_mode(unsigned char mode);
void dos_vga_enter(void);
void dos_vga_leave(void);
int dos_vga_active(void);
void dos_vga_set_rgb332_palette(void);
void dos_vga_wait_vblank(void);
unsigned long dos_vga_ticks(void);

/* Microseconds since boot, at roughly 838 ns resolution, by combining the
   18.2 Hz BIOS tick with a latched read of PIT channel 0. Wraps about every
   71 minutes, which is harmless for frame deltas. */
unsigned long dos_vga_micros(void);

void dos_vga_flush_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                        const gfx_color_t GFX_PTR *pixels,
                        void GFX_PTR *user);

/* Raw baseline support: present one complete 320x240 RGB565 frame only after
   the renderer has finished drawing every tile into a huge-memory buffer. */
void dos_vga_present_rgb565_frame(
    const gfx_color_t DOS_VGA_HUGE *pixels);

void dos_vga_set_flush_enabled(int enabled);
int dos_vga_flush_enabled(void);

#endif /* DOS_VGA_H */
