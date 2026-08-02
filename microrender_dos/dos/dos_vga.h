#ifndef DOS_VGA_H
#define DOS_VGA_H

#include "gfx.h"

/* Mode 13h plumbing shared by both DOS frontends.
 *
 * dos_app.c and dos_stress_app.c each carried their own copy of the video
 * mode switch, the vblank wait, the RGB332 palette upload, the BIOS tick
 * reader and the flush callback. They have now diverged slightly, which is
 * exactly the failure mode duplication produces: the stress copy indexed
 * source rows by tile width rather than by the renderer's tile stride, which
 * is correct only while every tile spans the full screen width.
 *
 * This is that code once, with the stride bug fixed.
 *
 * Open Watcom 16-bit real mode only.
 */

#define DOS_VGA_MODE_13H 0x13u
#define DOS_VGA_MODE_TEXT 0x03u

/* Mode 13h geometry. */
#define DOS_VGA_WIDTH 320
#define DOS_VGA_HEIGHT 200

/* Switch video modes directly. Prefer dos_vga_enter/leave, which track state. */
void dos_vga_set_mode(unsigned char mode);

/* Enter mode 13h and upload the RGB332 palette. Idempotent. */
void dos_vga_enter(void);

/* Restore text mode if dos_vga_enter() was called. Idempotent, and safe to
   call from an atexit() handler. */
void dos_vga_leave(void);

/* Non-zero while mode 13h is active. */
int dos_vga_active(void);

/* Upload the 256-entry RGB332 palette matching GFX_COLOR_INDEX8. */
void dos_vga_set_rgb332_palette(void);

/* Spin until the start of the next vertical blanking interval. */
void dos_vga_wait_vblank(void);

/* BIOS tick counter at 0040:006C, incrementing at 18.2 Hz. */
unsigned long dos_vga_ticks(void);

/* gfx_flush_fn that copies a finished tile into the mode 13h framebuffer.
   Pass this to gfx_init(). The `user` argument is ignored. */
void dos_vga_flush_tile(gfx_renderer_t GFX_PTR *r, int x, int y, int w, int h,
                        const gfx_color_t GFX_PTR *pixels,
                        void GFX_PTR *user);

/* Turn actual writes to video memory on or off without changing any other
   behaviour. The stress test uses this to measure rasterization cost with the
   VGA write bandwidth removed. Enabled by default. */
void dos_vga_set_flush_enabled(int enabled);
int dos_vga_flush_enabled(void);

#endif /* DOS_VGA_H */
