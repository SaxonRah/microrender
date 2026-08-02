#ifndef MR_STRBUF_H
#define MR_STRBUF_H

/* Minimal bounded string building for HUD and diagnostic text.
 *
 * Targets here either have no printf at all or have one that is far too
 * expensive to call per frame, so both the shared stress test and the Pico
 * frontend grew their own private copies of the same three append helpers.
 * This is that code, once.
 *
 * Every function takes the current write pointer and a one-past-the-end
 * pointer, writes as much as fits, and returns the new write pointer. Nothing
 * is ever written at or past `end`, so a caller that reserves one byte for the
 * terminator can always terminate safely:
 *
 *   char buf[64];
 *   char *p = buf;
 *   const char *end = buf + sizeof(buf) - 1;
 *   p = mr_strbuf_str(p, end, "fps ");
 *   p = mr_strbuf_u32(p, end, fps);
 *   *p = '\0';
 */

char *mr_strbuf_char(char *dst, const char *end, char c);
char *mr_strbuf_str(char *dst, const char *end, const char *src);
char *mr_strbuf_u32(char *dst, const char *end, unsigned long v);
char *mr_strbuf_i32(char *dst, const char *end, long v);

/* Fixed-point style decimal: value 685 with divisor 10 prints "68.5".
   Used for tenths-of-a-frame rates without pulling in floating point. */
char *mr_strbuf_frac(char *dst, const char *end, unsigned long scaled,
                     unsigned long divisor);

#endif /* MR_STRBUF_H */
