#include "mr_strbuf.h"

char *mr_strbuf_char(char *dst, const char *end, char c) {
  if (dst < end)
    *dst++ = c;
  return dst;
}

char *mr_strbuf_str(char *dst, const char *end, const char *src) {
  if (!src)
    return dst;
  while (*src && dst < end)
    *dst++ = *src++;
  return dst;
}

char *mr_strbuf_u32(char *dst, const char *end, unsigned long v) {
  char tmp[10];
  int n;

  if (v == 0ul)
    return mr_strbuf_char(dst, end, '0');

  /* Digits come out backwards, so buffer and reverse. tmp holds 10 digits,
     which covers the full range of a 32-bit unsigned long. */
  n = 0;
  while (v != 0ul && n < (int)sizeof(tmp)) {
    tmp[n++] = (char)('0' + (char)(v % 10ul));
    v /= 10ul;
  }
  while (n > 0)
    dst = mr_strbuf_char(dst, end, tmp[--n]);
  return dst;
}

char *mr_strbuf_i32(char *dst, const char *end, long v) {
  unsigned long mag;

  if (v < 0) {
    dst = mr_strbuf_char(dst, end, '-');
    /* Negating LONG_MIN is undefined, so convert through unsigned. */
    mag = (unsigned long)(-(v + 1)) + 1ul;
  } else {
    mag = (unsigned long)v;
  }
  return mr_strbuf_u32(dst, end, mag);
}

char *mr_strbuf_frac(char *dst, const char *end, unsigned long scaled,
                     unsigned long divisor) {
  if (divisor == 0ul)
    return mr_strbuf_u32(dst, end, scaled);
  dst = mr_strbuf_u32(dst, end, scaled / divisor);
  dst = mr_strbuf_char(dst, end, '.');
  return mr_strbuf_u32(dst, end, scaled % divisor);
}
