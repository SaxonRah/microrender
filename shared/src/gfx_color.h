#ifndef GFX_COLOR_H
#define GFX_COLOR_H

#include <stdint.h>

/* Pixel-format selection.

   Default: RGB565, used by RP2350 SPI LCDs and other 16-bit-color targets.

   DOS build: define GFX_COLOR_INDEX8=1 to make gfx_color_t an 8-bit palette
   index. The renderer core stays the same; only the pixel type changes.

   For INDEX8, GFX_RGB565(r,g,b) intentionally maps to an RGB332 palette index:
       rrrgggbb
   This gives DOS mode 13h a native 8-bit path while keeping most
   renderer/demo code source-compatible with RGB565 builds.
*/

#define GFX_COLOR_FORMAT_RGB565 1
#define GFX_COLOR_FORMAT_INDEX8 2

#if defined(GFX_COLOR_INDEX8) && (GFX_COLOR_INDEX8 != 0)

#define GFX_COLOR_FORMAT GFX_COLOR_FORMAT_INDEX8

typedef uint8_t gfx_color_t;

#define GFX_RGB332_INDEX(r, g, b)                                              \
  ((gfx_color_t)((((uint8_t)(r)) & 0xE0u) | ((((uint8_t)(g)) & 0xE0u) >> 3) |  \
                 ((((uint8_t)(b)) & 0xC0u) >> 6)))

#define GFX_RGB565(r, g, b) GFX_RGB332_INDEX((r), (g), (b))

#define GFX_RGB565_BLACK ((gfx_color_t)0x00u)
#define GFX_RGB565_WHITE ((gfx_color_t)0xFFu)
#define GFX_RGB565_RED ((gfx_color_t)0xE0u)
#define GFX_RGB565_GREEN ((gfx_color_t)0x1Cu)
#define GFX_RGB565_BLUE ((gfx_color_t)0x03u)
#define GFX_RGB565_YELLOW ((gfx_color_t)0xFCu)
#define GFX_RGB565_CYAN ((gfx_color_t)0x1Fu)
#define GFX_RGB565_MAGENTA ((gfx_color_t)0xE3u)

#else

#define GFX_COLOR_FORMAT GFX_COLOR_FORMAT_RGB565

typedef uint16_t gfx_color_t;

#define GFX_RGB565(r, g, b)                                                    \
  ((gfx_color_t)(((((uint16_t)(r)) & 0xF8u) << 8) |                            \
                 ((((uint16_t)(g)) & 0xFCu) << 3) |                            \
                 ((((uint16_t)(b)) & 0xF8u) >> 3)))

#define GFX_RGB565_BLACK ((gfx_color_t)0x0000u)
#define GFX_RGB565_WHITE ((gfx_color_t)0xFFFFu)
#define GFX_RGB565_RED ((gfx_color_t)0xF800u)
#define GFX_RGB565_GREEN ((gfx_color_t)0x07E0u)
#define GFX_RGB565_BLUE ((gfx_color_t)0x001Fu)
#define GFX_RGB565_YELLOW ((gfx_color_t)0xFFE0u)
#define GFX_RGB565_CYAN ((gfx_color_t)0x07FFu)
#define GFX_RGB565_MAGENTA ((gfx_color_t)0xF81Fu)

#endif

#endif /* GFX_COLOR_H */
