#ifndef GFX_RGB444_H
#define GFX_RGB444_H

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RGB565 -> packed 12-bit RGB444, for panels that accept a 12 bit/pixel
 * interface (ILI9341 COLMOD DBI=3, 0x53).
 *
 * Why bother: the wire, not the rasterizer, is what limits this renderer on
 * SPI-attached panels. A 320x240 RGB565 frame is 153,600 bytes. The same frame
 * at 12 bpp is 115,200 bytes -- 25% less to push, for free, every frame.
 *
 * Wire format is three bytes per two pixels, each channel in one nibble:
 *
 *     byte 0:  R0 R0 R0 R0  G0 G0 G0 G0
 *     byte 1:  B0 B0 B0 B0  R1 R1 R1 R1
 *     byte 2:  G1 G1 G1 G1  B1 B1 B1 B1
 *
 * An odd count emits a trailing half-pixel byte (R G) and reports the rounded
 * up byte count. Panels expect whole pixels, so callers should pack whole rows;
 * at a 320-pixel width this never arises.
 *
 * The conversion is lossy and one-way: 565 -> 444 discards one bit of red and
 * blue and two of green. Smooth gradients will band. This is a bandwidth
 * trade, not a free win.
 *
 * Returns the number of bytes written, or 0 if the arguments are unusable.
 * dst must have room for (count * 3 + 1) / 2 bytes.
 */
size_t gfx_pack_rgb444(uint8_t GFX_PTR *dst, const gfx_color_t GFX_PTR *src,
                       int count);

/* Bytes gfx_pack_rgb444 will write for a given pixel count. */
size_t gfx_rgb444_byte_count(int count);

#ifdef __cplusplus
}
#endif

#endif /* GFX_RGB444_H */
