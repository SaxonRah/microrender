#include "raylib.h"

#include "gfx.h"
#include "mr_autodemo.h"
#include "mr_demo_input.h"
#include "mr_game_demo.h"
#include "mr_stress_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MR_HOST_W 320
#define MR_HOST_H 240
#define MR_HOST_PIXELS (MR_HOST_W * MR_HOST_H)
#define MR_HOST_MODE_RAW 0
#define MR_HOST_MODE_TILED 1
#define MR_HOST_MODE_LACE 2
#define MR_HOST_MODE_DIRTYRECT 3
#define MR_HOST_DEMO_GAME 0
#define MR_HOST_DEMO_STRESS 1

#ifndef MR_RAYLIB_DEFAULT_DEMO
#define MR_RAYLIB_DEFAULT_DEMO MR_HOST_DEMO_GAME
#endif
#ifndef MR_RAYLIB_DEFAULT_MODE
#define MR_RAYLIB_DEFAULT_MODE MR_HOST_MODE_TILED
#endif
#ifndef MR_RAYLIB_DEFAULT_TILE_H
#define MR_RAYLIB_DEFAULT_TILE_H 240
#endif
#ifndef MR_RAYLIB_DEFAULT_SCALE
#define MR_RAYLIB_DEFAULT_SCALE 3
#endif
#ifndef MR_RAYLIB_DEFAULT_SPRITES
#define MR_RAYLIB_DEFAULT_SPRITES 512
#endif
#ifndef MR_RAYLIB_DEFAULT_FPS
#define MR_RAYLIB_DEFAULT_FPS 0
#endif
#ifndef MR_RAYLIB_DEFAULT_AUTOPLAY
#define MR_RAYLIB_DEFAULT_AUTOPLAY 0
#endif
#ifndef MR_RAYLIB_DEFAULT_LACE_BLOCK_H
#define MR_RAYLIB_DEFAULT_LACE_BLOCK_H 4
#endif

typedef struct host_options {
  int demo;
  int mode;
  int tile_h;
  int scale;
  int sprites;
  int target_fps;
  int frames_limit;
  int autoplay;
  int lace_block_h;
} host_options_t;

typedef struct host_state {
  host_options_t opt;
  gfx_renderer_t renderer;
  gfx_color_t *tile_a;
  gfx_color_t *frame;
  gfx_color_t *staging;
  mr_game_demo_t game;
  mr_stress_test_t stress;
  Texture2D texture;
  int texture_ready;
  unsigned long frames;
  double start_time;
} host_state_t;

static int arg_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int parse_int(int argc, char **argv, int *index, int fallback) {
  if (*index + 1 >= argc)
    return fallback;
  ++(*index);
  return atoi(argv[*index]);
}

static void print_usage(void) {
  printf("MicroRender Raylib frontend (320x240 RGB565)\n\n");
  printf("Usage: microrender_raylib [options]\n");
  printf("  --demo game|stress\n");
  printf("  --mode raw|tiled|lace|dirtyrect\n");
  printf("  --tile N       renderer tile height (raw/lace/dirty force 240)\n");
  printf("  --scale N      integer window scale\n");
  printf("  --sprites N    stress sprites\n");
  printf("  --fps N        target FPS; 0 = uncapped\n");
  printf("  --frames N     exit after N frames\n");
  printf("  --autoplay     scripted game input\n");
  printf("  --lace-block N alternating row-group height\n");
}

static int parse_mode(const char *s) {
  if (arg_eq(s, "raw"))
    return MR_HOST_MODE_RAW;
  if (arg_eq(s, "lace"))
    return MR_HOST_MODE_LACE;
  if (arg_eq(s, "dirtyrect"))
    return MR_HOST_MODE_DIRTYRECT;
  return MR_HOST_MODE_TILED;
}

static const char *mode_name(int mode) {
  if (mode == MR_HOST_MODE_RAW)
    return "raw";
  if (mode == MR_HOST_MODE_LACE)
    return "lace";
  if (mode == MR_HOST_MODE_DIRTYRECT)
    return "dirtyrect";
  return "tiled";
}

static void parse_options(int argc, char **argv, host_options_t *opt) {
  int i;
  opt->demo = MR_RAYLIB_DEFAULT_DEMO;
  opt->mode = MR_RAYLIB_DEFAULT_MODE;
  opt->tile_h = MR_RAYLIB_DEFAULT_TILE_H;
  opt->scale = MR_RAYLIB_DEFAULT_SCALE;
  opt->sprites = MR_RAYLIB_DEFAULT_SPRITES;
  opt->target_fps = MR_RAYLIB_DEFAULT_FPS;
  opt->frames_limit = 0;
  opt->autoplay = MR_RAYLIB_DEFAULT_AUTOPLAY ? 1 : 0;
  opt->lace_block_h = MR_RAYLIB_DEFAULT_LACE_BLOCK_H;

  for (i = 1; i < argc; ++i) {
    if (arg_eq(argv[i], "--help") || arg_eq(argv[i], "-h")) {
      print_usage();
      exit(0);
    } else if (arg_eq(argv[i], "--demo") && i + 1 < argc) {
      ++i;
      opt->demo = arg_eq(argv[i], "stress") ? MR_HOST_DEMO_STRESS
                                             : MR_HOST_DEMO_GAME;
    } else if (arg_eq(argv[i], "--mode") && i + 1 < argc) {
      ++i;
      opt->mode = parse_mode(argv[i]);
    } else if (arg_eq(argv[i], "--tile")) {
      opt->tile_h = parse_int(argc, argv, &i, opt->tile_h);
    } else if (arg_eq(argv[i], "--scale")) {
      opt->scale = parse_int(argc, argv, &i, opt->scale);
    } else if (arg_eq(argv[i], "--sprites")) {
      opt->sprites = parse_int(argc, argv, &i, opt->sprites);
    } else if (arg_eq(argv[i], "--fps")) {
      opt->target_fps = parse_int(argc, argv, &i, opt->target_fps);
    } else if (arg_eq(argv[i], "--frames")) {
      opt->frames_limit = parse_int(argc, argv, &i, opt->frames_limit);
    } else if (arg_eq(argv[i], "--lace-block")) {
      opt->lace_block_h = parse_int(argc, argv, &i, opt->lace_block_h);
    } else if (arg_eq(argv[i], "--autoplay")) {
      opt->autoplay = 1;
    }
  }

  if (opt->scale < 1)
    opt->scale = 1;
  if (opt->scale > 8)
    opt->scale = 8;
  if (opt->tile_h < 1)
    opt->tile_h = 1;
  if (opt->tile_h > MR_HOST_H)
    opt->tile_h = MR_HOST_H;
  if (opt->sprites < 1)
    opt->sprites = 1;
  if (opt->sprites > MR_STRESS_MAX_SPRITES)
    opt->sprites = MR_STRESS_MAX_SPRITES;
  if (opt->lace_block_h < 1)
    opt->lace_block_h = 1;
  if (opt->lace_block_h > MR_HOST_H)
    opt->lace_block_h = MR_HOST_H;
  if (opt->mode == MR_HOST_MODE_RAW || opt->mode == MR_HOST_MODE_LACE ||
      opt->mode == MR_HOST_MODE_DIRTYRECT)
    opt->tile_h = MR_HOST_H;
}

static void host_flush(gfx_renderer_t *r, int x, int y, int w, int h,
                       const gfx_color_t *pixels, void *user) {
  host_state_t *host = (host_state_t *)user;
  int row;
  int stride;
  if (!host || !pixels || w <= 0 || h <= 0)
    return;
  if (x < 0 || y < 0 || x + w > MR_HOST_W || y + h > MR_HOST_H)
    return;
  stride = r ? r->tile_stride : w;

  if (host->opt.mode == MR_HOST_MODE_TILED && host->texture_ready) {
    Rectangle rect;
    /* The core flushes full-width horizontal tiles, so the tile rows are
       contiguous and can be uploaded immediately. Keep the fallback copy for
       clipped/custom flushes to make the frontend robust to future callers. */
    if (x == 0 && w == MR_HOST_W && stride == w) {
      rect.x = 0.0f;
      rect.y = (float)y;
      rect.width = (float)w;
      rect.height = (float)h;
      UpdateTextureRec(host->texture, rect, pixels);
      return;
    }
  }

  for (row = 0; row < h; ++row) {
    memcpy(host->staging + (y + row) * MR_HOST_W + x,
           pixels + row * stride, (size_t)w * sizeof(gfx_color_t));
  }
}

static void draw_game(gfx_renderer_t *r, void *user) {
  host_state_t *host = (host_state_t *)user;
  mr_game_demo_render(&host->game, r);
}

static void draw_stress(gfx_renderer_t *r, void *user) {
  host_state_t *host = (host_state_t *)user;
  mr_stress_render(r, &host->stress);
}

static void host_input(mr_demo_input_t *input) {
  memset(input, 0, sizeof(*input));
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
    input->buttons |= MR_DEMO_INPUT_LEFT;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
    input->buttons |= MR_DEMO_INPUT_RIGHT;
  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
    input->buttons |= MR_DEMO_INPUT_UP;
  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
    input->buttons |= MR_DEMO_INPUT_DOWN;
  if (IsKeyPressed(KEY_ENTER))
    input->buttons |= MR_DEMO_INPUT_START;
  if (IsKeyPressed(KEY_SPACE))
    input->buttons |= MR_DEMO_INPUT_ACTION;
  if (IsKeyPressed(KEY_P))
    input->buttons |= MR_DEMO_INPUT_PAUSE;
  if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_GRAVE))
    input->buttons |= MR_DEMO_INPUT_DEBUG;
  if (IsKeyPressed(KEY_ESCAPE))
    input->buttons |= MR_DEMO_INPUT_QUIT;
  input->dx = ((input->buttons & MR_DEMO_INPUT_RIGHT) ? 1 : 0) -
              ((input->buttons & MR_DEMO_INPUT_LEFT) ? 1 : 0);
  input->dy = ((input->buttons & MR_DEMO_INPUT_DOWN) ? 1 : 0) -
              ((input->buttons & MR_DEMO_INPUT_UP) ? 1 : 0);
}

static void present_lace(host_state_t *host, Texture2D texture) {
  int phase = (int)(host->frames & 1ul);
  int y;
  int block = host->opt.lace_block_h;
  for (y = phase * block; y < MR_HOST_H; y += block * 2) {
    int h = block;
    Rectangle rect;
    if (y + h > MR_HOST_H)
      h = MR_HOST_H - y;
    memcpy(host->frame + y * MR_HOST_W, host->staging + y * MR_HOST_W,
           (size_t)MR_HOST_W * (size_t)h * sizeof(gfx_color_t));
    rect.x = 0.0f;
    rect.y = (float)y;
    rect.width = (float)MR_HOST_W;
    rect.height = (float)h;
    UpdateTextureRec(texture, rect, host->frame + y * MR_HOST_W);
  }
}

static void present_dirty(host_state_t *host, Texture2D texture) {
  int x;
  int y;
  int min_x = MR_HOST_W;
  int min_y = MR_HOST_H;
  int max_x = -1;
  int max_y = -1;

  /* Find one bounding rectangle for all changed RGB565 pixels. The completed
     render tile is no longer needed at this point, so reuse tile_a as a tightly
     packed upload buffer instead of allocating another full frame. */
  for (y = 0; y < MR_HOST_H; ++y) {
    const gfx_color_t *old_row = host->frame + y * MR_HOST_W;
    const gfx_color_t *new_row = host->staging + y * MR_HOST_W;
    for (x = 0; x < MR_HOST_W; ++x) {
      if (old_row[x] != new_row[x]) {
        if (x < min_x)
          min_x = x;
        if (x > max_x)
          max_x = x;
        if (y < min_y)
          min_y = y;
        if (y > max_y)
          max_y = y;
      }
    }
  }

  if (max_x >= min_x && max_y >= min_y) {
    int rect_w = max_x - min_x + 1;
    int rect_h = max_y - min_y + 1;
    Rectangle rect;
    for (y = 0; y < rect_h; ++y) {
      gfx_color_t *dst = host->frame + (min_y + y) * MR_HOST_W + min_x;
      const gfx_color_t *src =
          host->staging + (min_y + y) * MR_HOST_W + min_x;
      memcpy(dst, src, (size_t)rect_w * sizeof(gfx_color_t));
      memcpy(host->tile_a + y * rect_w, src,
             (size_t)rect_w * sizeof(gfx_color_t));
    }
    rect.x = (float)min_x;
    rect.y = (float)min_y;
    rect.width = (float)rect_w;
    rect.height = (float)rect_h;
    UpdateTextureRec(texture, rect, host->tile_a);
  }
}

int main(int argc, char **argv) {
  host_state_t host;
  mr_stress_config_t stress_cfg;
  Image image;
  Texture2D texture;
  Rectangle src;
  Rectangle dst;
  size_t tile_pixels;

  memset(&host, 0, sizeof(host));
  parse_options(argc, argv, &host.opt);
  tile_pixels = (size_t)MR_HOST_W * (size_t)host.opt.tile_h;
  host.tile_a = (gfx_color_t *)calloc(tile_pixels, sizeof(gfx_color_t));
  host.frame = (gfx_color_t *)calloc(MR_HOST_PIXELS, sizeof(gfx_color_t));
  host.staging = (gfx_color_t *)calloc(MR_HOST_PIXELS, sizeof(gfx_color_t));
  if (!host.tile_a || !host.frame || !host.staging) {
    fprintf(stderr, "MicroRender: out of memory\n");
    return 1;
  }

  InitWindow(MR_HOST_W * host.opt.scale, MR_HOST_H * host.opt.scale,
             "MicroRender Raylib - 320x240 RGB565");
  if (host.opt.target_fps > 0)
    SetTargetFPS(host.opt.target_fps);

  image.data = host.frame;
  image.width = MR_HOST_W;
  image.height = MR_HOST_H;
  image.mipmaps = 1;
  image.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;
  texture = LoadTextureFromImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_POINT);
  host.texture = texture;
  host.texture_ready = 1;

  gfx_init(&host.renderer, MR_HOST_W, MR_HOST_H, host.tile_a, host.opt.tile_h,
           host_flush, &host);

  if (host.opt.demo == MR_HOST_DEMO_STRESS) {
    mr_stress_config_defaults(&stress_cfg, MR_HOST_W, MR_HOST_H);
    stress_cfg.sprite_count = host.opt.sprites;
    stress_cfg.features = MR_STRESS_FEATURE_DEFAULT;
    mr_stress_init(&host.stress, &stress_cfg);
  } else {
    mr_game_demo_init(&host.game, MR_HOST_W, MR_HOST_H);
    mr_autodemo_reset();
  }

  host.start_time = GetTime();
  printf("MicroRender Raylib: 320x240 RGB565 demo=%s mode=%s tile=%d sprites=%d\n",
         host.opt.demo == MR_HOST_DEMO_GAME ? "game" : "stress",
         mode_name(host.opt.mode), host.opt.tile_h, host.opt.sprites);

  src.x = 0.0f;
  src.y = 0.0f;
  src.width = (float)MR_HOST_W;
  src.height = (float)MR_HOST_H;
  dst.x = 0.0f;
  dst.y = 0.0f;
  dst.width = (float)(MR_HOST_W * host.opt.scale);
  dst.height = (float)(MR_HOST_H * host.opt.scale);

  while (!WindowShouldClose()) {
    unsigned long fps10;
    unsigned long avg10;
    double elapsed;
    float frame_time;

    frame_time = GetFrameTime();
    elapsed = GetTime() - host.start_time;
    fps10 = frame_time > 0.0f
                ? (unsigned long)(10.0 / (double)frame_time)
                : 0ul;
    avg10 = elapsed > 0.0 ? (unsigned long)((double)host.frames * 10.0 / elapsed)
                          : 0ul;
    if (host.opt.demo == MR_HOST_DEMO_GAME)
      mr_game_demo_set_fps10(&host.game, fps10, avg10);
    else
      mr_stress_set_fps10(&host.stress, fps10, avg10);

    if (host.opt.demo == MR_HOST_DEMO_GAME) {
      mr_demo_input_t input;
      if (host.opt.autoplay)
        mr_autodemo_input(host.frames, &input);
      else
        host_input(&input);
      mr_game_demo_tick(&host.game, &input);
      if (mr_game_demo_quit_requested(&host.game))
        break;
    } else {
      mr_stress_tick(&host.stress);
    }

    if (host.opt.demo == MR_HOST_DEMO_GAME) {
      gfx_render_tiled(&host.renderer, draw_game, &host, GFX_RGB565_BLACK);
    } else {
      gfx_render_tiled(&host.renderer, draw_stress, &host, GFX_RGB565_BLACK);
    }

    if (host.opt.mode == MR_HOST_MODE_LACE) {
      present_lace(&host, texture);
    } else if (host.opt.mode == MR_HOST_MODE_DIRTYRECT) {
      present_dirty(&host, texture);
    } else if (host.opt.mode == MR_HOST_MODE_RAW) {
      /* Deliberately serialized baseline: the complete 320x240 image is drawn
         first, then one full RGB565 upload occurs, then the loop continues. */
      memcpy(host.frame, host.staging, MR_HOST_PIXELS * sizeof(gfx_color_t));
      UpdateTexture(texture, host.frame);
    } else {
      /* Tiled mode uploads each completed strip directly from host_flush(). */
    }

    ++host.frames;

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    EndDrawing();

    if (host.opt.frames_limit > 0 &&
        host.frames >= (unsigned long)host.opt.frames_limit)
      break;
  }

  host.texture_ready = 0;
  UnloadTexture(texture);
  CloseWindow();
  free(host.tile_a);
  free(host.frame);
  free(host.staging);
  return 0;
}
