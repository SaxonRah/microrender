#ifndef GFX_CONFIG_H
#define GFX_CONFIG_H

/* MicroRender C99 configuration.

   DOS 16-bit large-model note:
   - Do not put a full 320x240 RGB565 framebuffer in one object.
   - The default 320x16 tile is 10,240 bytes and fits under one 64 KiB segment.
   - In Open Watcom large model, ordinary data pointers are far by default.
   - If your compiler needs explicit far annotations, define GFX_PTR before
     including the renderer headers, e.g. #define GFX_PTR __far.
*/

#ifndef GFX_INLINE
#if defined(__GNUC__)
#define GFX_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define GFX_INLINE static __inline
#else
#define GFX_INLINE static
#endif
#endif

#ifndef GFX_PTR
#define GFX_PTR
#endif

#ifndef GFX_ROM
#define GFX_ROM const
#endif

#ifndef GFX_RESTRICT
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define GFX_RESTRICT restrict
#else
#define GFX_RESTRICT
#endif
#endif

/* 16-bit int detection.

   Open Watcom's DOS targets use a 16-bit int even in the large memory model,
   where only pointers become far. Row-addressing math in the blitters is int
   math, so the renderer needs to know this to cap surface sizes rather than
   wrap silently. Override by defining GFX_INT_IS_16BIT yourself. */
#ifndef GFX_INT_IS_16BIT
#include <limits.h>
#if INT_MAX < 2147483647L
#define GFX_INT_IS_16BIT 1
#else
#define GFX_INT_IS_16BIT 0
#endif
#endif

#ifndef GFX_DEFAULT_TILE_H
#define GFX_DEFAULT_TILE_H 16
#endif

#ifndef GFX_ENABLE_TRIANGLES
#define GFX_ENABLE_TRIANGLES 1
#endif

#ifndef GFX_DIRTY_MAX_RECTS
#define GFX_DIRTY_MAX_RECTS 16
#endif

#endif /* GFX_CONFIG_H */
