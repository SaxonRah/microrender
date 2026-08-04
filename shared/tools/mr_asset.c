/*
   mr_asset - MicroRender indexed asset converter.

   Host-side C99 legacy source-asset tool. Converts 8-bit indexed BMP or PCX
   files into C arrays for the optional indexed-data pipeline. Shipping Pico,
   DOS, and Raylib frontends render RGB565; indexed source art must be mapped
   through its palette before it becomes a shipping gfx_color_t sprite. The tool
   can emit raw indices or RLE masked indices where one palette index is treated
   as transparent.

   Usage:
     mr_asset input.bmp output_name [--raw] [--rle] [--key N]
     mr_asset input.pcx output_name --rle --key 0
*/
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MR_ASSET_MAX_NAME
#define MR_ASSET_MAX_NAME 64
#endif

typedef struct Image8 {
  int w;
  int h;
  uint8_t *pixels;
} Image8;

static uint16_t rd16le(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static int32_t rd32s(const uint8_t *p) { return (int32_t)rd32le(p); }

static void free_image(Image8 *img) {
  if (img && img->pixels)
    free(img->pixels);
  if (img) {
    img->pixels = 0;
    img->w = img->h = 0;
  }
}

static int load_file(const char *path, uint8_t **out, long *out_len) {
  FILE *f = fopen(path, "rb");
  long len;
  uint8_t *buf;
  if (!f)
    return 0;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 0;
  }
  len = ftell(f);
  if (len <= 0) {
    fclose(f);
    return 0;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  buf = (uint8_t *)malloc((size_t)len);
  if (!buf) {
    fclose(f);
    return 0;
  }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    fclose(f);
    return 0;
  }
  fclose(f);
  *out = buf;
  *out_len = len;
  return 1;
}

static int load_bmp8(const char *path, Image8 *img) {
  uint8_t *buf = 0;
  long len = 0;
  uint32_t pixel_off;
  int32_t w;
  int32_t h_signed;
  int h;
  uint16_t bpp;
  uint32_t comp;
  int top_down;
  int row_stride;
  int y;

  memset(img, 0, sizeof(*img));
  if (!load_file(path, &buf, &len))
    return 0;
  if (len < 54 || buf[0] != 'B' || buf[1] != 'M') {
    free(buf);
    return 0;
  }
  pixel_off = rd32le(buf + 10);
  w = rd32s(buf + 18);
  h_signed = rd32s(buf + 22);
  bpp = rd16le(buf + 28);
  comp = rd32le(buf + 30);
  if (w <= 0 || h_signed == 0 || bpp != 8 || comp != 0) {
    free(buf);
    return 0;
  }
  top_down = h_signed < 0;
  h = top_down ? -h_signed : h_signed;
  row_stride = (int)(((w + 3) / 4) * 4);
  if ((long)pixel_off + (long)row_stride * h > len) {
    free(buf);
    return 0;
  }
  img->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
  if (!img->pixels) {
    free(buf);
    return 0;
  }
  img->w = (int)w;
  img->h = h;
  for (y = 0; y < h; ++y) {
    int src_y = top_down ? y : (h - 1 - y);
    memcpy(img->pixels + (size_t)y * img->w,
           buf + pixel_off + (long)src_y * row_stride, (size_t)img->w);
  }
  free(buf);
  return 1;
}

typedef struct PcxHeader {
  uint8_t manufacturer;
  uint8_t version;
  uint8_t encoding;
  uint8_t bits_per_pixel;
  uint16_t xmin, ymin, xmax, ymax;
  uint16_t hdpi, vdpi;
  uint8_t colormap[48];
  uint8_t reserved;
  uint8_t planes;
  uint16_t bytes_per_line;
  uint16_t palette_info;
  uint16_t hscreen, vscreen;
  uint8_t filler[54];
} PcxHeader;

static int load_pcx8(const char *path, Image8 *img) {
  uint8_t *buf = 0;
  long len = 0;
  PcxHeader ph;
  int w, h;
  long pos;
  int y;

  memset(img, 0, sizeof(*img));
  if (!load_file(path, &buf, &len))
    return 0;
  if (len < 128) {
    free(buf);
    return 0;
  }
  memcpy(&ph, buf, 128);
  ph.xmin = rd16le(buf + 4);
  ph.ymin = rd16le(buf + 6);
  ph.xmax = rd16le(buf + 8);
  ph.ymax = rd16le(buf + 10);
  ph.bytes_per_line = rd16le(buf + 66);
  if (ph.manufacturer != 10 || ph.encoding != 1 || ph.bits_per_pixel != 8 ||
      ph.planes != 1) {
    free(buf);
    return 0;
  }
  w = (int)ph.xmax - (int)ph.xmin + 1;
  h = (int)ph.ymax - (int)ph.ymin + 1;
  if (w <= 0 || h <= 0 || ph.bytes_per_line < (uint16_t)w) {
    free(buf);
    return 0;
  }
  img->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
  if (!img->pixels) {
    free(buf);
    return 0;
  }
  img->w = w;
  img->h = h;
  pos = 128;
  for (y = 0; y < h; ++y) {
    int x = 0;
    int out_x = 0;
    while (x < ph.bytes_per_line && pos < len) {
      uint8_t b = buf[pos++];
      int count;
      uint8_t val;
      if ((b & 0xC0u) == 0xC0u) {
        count = b & 0x3Fu;
        if (pos >= len) {
          free_image(img);
          free(buf);
          return 0;
        }
        val = buf[pos++];
      } else {
        count = 1;
        val = b;
      }
      while (count-- > 0 && x < ph.bytes_per_line) {
        if (out_x < w)
          img->pixels[(size_t)y * w + out_x] = val;
        ++x;
        ++out_x;
      }
    }
  }
  free(buf);
  return 1;
}

static int ascii_ieq(const char *a, const char *b) {
  while (*a && *b) {
    unsigned char ca = (unsigned char)*a++;
    unsigned char cb = (unsigned char)*b++;
    if (tolower(ca) != tolower(cb))
      return 0;
  }
  return *a == 0 && *b == 0;
}

static int has_ext(const char *p, const char *ext) {
  size_t lp = strlen(p), le = strlen(ext);
  if (lp < le)
    return 0;
  return ascii_ieq(p + lp - le, ext);
}

static void make_symbol(const char *in, char *out) {
  int n = 0;
  int i;
  for (i = 0; in[i] && n < MR_ASSET_MAX_NAME - 1; ++i) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c))
      out[n++] = (char)tolower(c);
    else
      out[n++] = '_';
  }
  out[n] = 0;
  if (!out[0] || isdigit((unsigned char)out[0])) {
    memmove(out + 3, out, strlen(out) + 1);
    out[0] = 'm';
    out[1] = 'r';
    out[2] = '_';
  }
}

static void emit_raw(FILE *f, const Image8 *img, const char *name, int key) {
  int i;
  fprintf(f, "#include \"gfx.h\"\n\n");
  fprintf(f, "static const gfx_color_t %s_pixels[%d] = {\n", name,
          img->w * img->h);
  for (i = 0; i < img->w * img->h; ++i) {
    if ((i % 16) == 0)
      fprintf(f, "    ");
    fprintf(f, "0x%02Xu", img->pixels[i]);
    if (i + 1 != img->w * img->h)
      fprintf(f, ", ");
    if ((i % 16) == 15)
      fprintf(f, "\n");
  }
  if ((img->w * img->h % 16) != 0)
    fprintf(f, "\n");
  fprintf(f, "};\n\n");
  fprintf(f,
          "const gfx_sprite_t %s_sprite = { %d, %d, %s_pixels, 0, 0, "
          "(gfx_color_t)%d, %s };\n",
          name, img->w, img->h, name, key,
          key >= 0 ? "GFX_SPRITE_COLORKEY" : "0");
}

static void emit_rle(FILE *f, const Image8 *img, const char *name, int key) {
  int y, x;
  int run_count = 0;
  int pixel_count = 0;
  typedef struct Run {
    int x, y, len, off;
  } Run;
  Run *runs = (Run *)malloc((size_t)img->w * (size_t)img->h * sizeof(Run));
  uint8_t *pix = (uint8_t *)malloc((size_t)img->w * (size_t)img->h);
  int i;
  if (!runs || !pix) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }
  for (y = 0; y < img->h; ++y) {
    x = 0;
    while (x < img->w) {
      int start, len;
      while (x < img->w && img->pixels[y * img->w + x] == (uint8_t)key)
        ++x;
      if (x >= img->w)
        break;
      start = x;
      while (x < img->w && img->pixels[y * img->w + x] != (uint8_t)key)
        ++x;
      len = x - start;
      runs[run_count].x = start;
      runs[run_count].y = y;
      runs[run_count].len = len;
      runs[run_count].off = pixel_count;
      memcpy(pix + pixel_count, img->pixels + y * img->w + start, (size_t)len);
      pixel_count += len;
      ++run_count;
    }
  }

  fprintf(f, "#include \"gfx.h\"\n\n");
  fprintf(f, "static const gfx_color_t %s_rle_pixels[%d] = {\n", name,
          pixel_count);
  for (i = 0; i < pixel_count; ++i) {
    if ((i % 16) == 0)
      fprintf(f, "    ");
    fprintf(f, "0x%02Xu", pix[i]);
    if (i + 1 != pixel_count)
      fprintf(f, ", ");
    if ((i % 16) == 15)
      fprintf(f, "\n");
  }
  if ((pixel_count % 16) != 0)
    fprintf(f, "\n");
  fprintf(f, "};\n\n");
  fprintf(f, "static const gfx_rle_run_t %s_runs[%d] = {\n", name, run_count);
  for (i = 0; i < run_count; ++i) {
    fprintf(f, "    { %d, %d, %d, %du }%s\n", runs[i].x, runs[i].y, runs[i].len,
            runs[i].off, i + 1 == run_count ? "" : ",");
  }
  fprintf(f, "};\n\n");
  fprintf(f,
          "const gfx_sprite_t %s_sprite = { %d, %d, %s_rle_pixels, %s_runs, "
          "%d, (gfx_color_t)%d, GFX_SPRITE_RLE };\n",
          name, img->w, img->h, name, name, run_count, key);
  free(runs);
  free(pix);
}

int main(int argc, char **argv) {
  Image8 img;
  int emit_rle_mode = 1;
  int key = 0;
  int i;
  char sym[MR_ASSET_MAX_NAME];
  char outpath[256];
  FILE *out;
  if (argc < 3) {
    fprintf(stderr, "usage: mr_asset input.bmp|input.pcx output_name "
                    "[--raw|--rle] [--key N]\n");
    return 1;
  }
  for (i = 3; i < argc; ++i) {
    if (strcmp(argv[i], "--raw") == 0)
      emit_rle_mode = 0;
    else if (strcmp(argv[i], "--rle") == 0)
      emit_rle_mode = 1;
    else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc)
      key = atoi(argv[++i]);
  }
  if (key < 0)
    key = 0;
  if (key > 255)
    key = 255;
  if (has_ext(argv[1], ".bmp")) {
    if (!load_bmp8(argv[1], &img)) {
      fprintf(stderr, "failed to load 8-bit uncompressed BMP\n");
      return 1;
    }
  } else if (has_ext(argv[1], ".pcx")) {
    if (!load_pcx8(argv[1], &img)) {
      fprintf(stderr, "failed to load 8-bit PCX\n");
      return 1;
    }
  } else {
    fprintf(stderr, "unknown input extension; expected .bmp or .pcx\n");
    return 1;
  }
  make_symbol(argv[2], sym);
  snprintf(outpath, sizeof(outpath), "%s.h", argv[2]);
  out = fopen(outpath, "wb");
  if (!out) {
    fprintf(stderr, "could not open output\n");
    free_image(&img);
    return 1;
  }
  fprintf(out, "/* Generated by mr_asset from %s. */\n", argv[1]);
  if (emit_rle_mode)
    emit_rle(out, &img, sym, key);
  else
    emit_raw(out, &img, sym, key);
  fclose(out);
  printf("wrote %s (%dx%d, %s, key=%d)\n", outpath, img.w, img.h,
         emit_rle_mode ? "rle" : "raw", key);
  free_image(&img);
  return 0;
}
