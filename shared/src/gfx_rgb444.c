#include "gfx_rgb444.h"

size_t gfx_rgb444_byte_count(int count) {
  if (count <= 0)
    return 0u;
  return ((size_t)count * 3u + 1u) / 2u;
}

#if GFX_COLOR_FORMAT == GFX_COLOR_FORMAT_RGB565

/* RGB565 -> RGB444 per channel: drop the low bit of red and blue and the low
   two bits of green. Truncation rather than rounding, so a saturated channel
   stays saturated (0x1F >> 1 == 0x0F) and black stays black. Rounding would
   need a clamp on the top end and buys nothing visible at four bits. */
#define GFX_565_R4(c) ((uint8_t)(((c) >> 12) & 0x0Fu))
#define GFX_565_G4(c) ((uint8_t)(((c) >> 7) & 0x0Fu))
#define GFX_565_B4(c) ((uint8_t)(((c) >> 1) & 0x0Fu))

size_t gfx_pack_rgb444(uint8_t GFX_PTR *dst, const gfx_color_t GFX_PTR *src,
                       int count) {
  int i;
  size_t out = 0u;

  if (!dst || !src || count <= 0)
    return 0u;

  /* Two pixels per iteration is the natural unit: it is exactly three whole
     bytes, so the loop never carries a partial byte between iterations. */
  for (i = 0; i + 1 < count; i += 2) {
    gfx_color_t a = src[i];
    gfx_color_t b = src[i + 1];
    dst[out + 0] = (uint8_t)((GFX_565_R4(a) << 4) | GFX_565_G4(a));
    dst[out + 1] = (uint8_t)((GFX_565_B4(a) << 4) | GFX_565_R4(b));
    dst[out + 2] = (uint8_t)((GFX_565_G4(b) << 4) | GFX_565_B4(b));
    out += 3u;
  }

  if (i < count) {
    gfx_color_t a = src[i];
    dst[out + 0] = (uint8_t)((GFX_565_R4(a) << 4) | GFX_565_G4(a));
    dst[out + 1] = (uint8_t)(GFX_565_B4(a) << 4);
    out += 2u;
  }

  return out;
}

#else /* INDEX8 */

size_t gfx_pack_rgb444(uint8_t GFX_PTR *dst, const gfx_color_t GFX_PTR *src,
                       int count) {
  /* The indexed build has no direct-colour value to pack; a palette lookup
     would be required and no frontend needs one. Fail loudly rather than
     emit plausible garbage. */
  (void)dst;
  (void)src;
  (void)count;
  return 0u;
}

#endif
