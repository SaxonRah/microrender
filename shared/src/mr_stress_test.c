#include "mr_stress_test.h"
#include "mr_strbuf.h"
#include <string.h>

#ifndef MR_STRESS_HUD_MODE
/* 0 = no HUD, 1 = full two-line HUD, 2 = clean one-line HUD. */
#define MR_STRESS_HUD_MODE 1
#endif


static void mr_stress_finish_buf(char *buf, char *dst, char *end) {
  if (dst < end) {
    *dst = '\0';
  } else if (end > buf) {
    end[-1] = '\0';
  } else if (buf) {
    *buf = '\0';
  }
}

static void mr_stress_format_hud_line(char *buf, int bufsz,
                                      const mr_stress_test_t GFX_PTR *st,
                                      int full) {
  char *dst;
  char *end;

  if (!buf || bufsz <= 0)
    return;

  dst = buf;
  end = buf + bufsz - 1;

  dst = mr_strbuf_str(dst, end, "FPS");
  dst = mr_strbuf_u32(dst, end, st->metrics.fps10 / 10ul);
  dst = mr_strbuf_char(dst, end, '.');
  dst = mr_strbuf_u32(dst, end, st->metrics.fps10 % 10ul);
  dst = mr_strbuf_str(dst, end, " AVG");
  dst = mr_strbuf_u32(dst, end, st->metrics.avg_fps10 / 10ul);
  dst = mr_strbuf_char(dst, end, '.');
  dst = mr_strbuf_u32(dst, end, st->metrics.avg_fps10 % 10ul);
  dst = mr_strbuf_str(dst, end, " S");
  dst = mr_strbuf_u32(dst, end, (unsigned long)st->cfg.sprite_count);
  dst = mr_strbuf_str(dst, end, " V");
  dst = mr_strbuf_u32(dst, end, st->metrics.sprites_visible);

  if (full) {
    dst = mr_strbuf_str(dst, end, " B");
    dst = mr_strbuf_u32(dst, end, st->metrics.bucket_items);
  }

  mr_stress_finish_buf(buf, dst, end + 1);
}

static void mr_stress_format_hud_detail(char *buf, int bufsz,
                                        const mr_stress_test_t GFX_PTR *st) {
  char *dst;
  char *end;

  if (!buf || bufsz <= 0)
    return;

  dst = buf;
  end = buf + bufsz - 1;

  dst = mr_strbuf_str(dst, end, "D");
  dst = mr_strbuf_u32(dst, end, st->metrics.sprites_drawn);
  dst = mr_strbuf_str(dst, end, " R");
  dst = mr_strbuf_u32(dst, end, st->metrics.sprites_rejected);
  dst = mr_strbuf_str(dst, end, " RN");
  dst = mr_strbuf_u32(dst, end, st->metrics.rle_runs_drawn);
  dst = mr_strbuf_str(dst, end, " PX");
  dst = mr_strbuf_u32(dst, end, st->metrics.rle_pixels_copied);
  dst = mr_strbuf_str(dst, end, " C");
  dst = mr_strbuf_u32(dst, end, st->metrics.collision_hits);
  dst = mr_strbuf_char(dst, end, '/');
  dst = mr_strbuf_u32(dst, end, st->metrics.collision_checks);

  mr_stress_finish_buf(buf, dst, end + 1);
}

static int mr_stress_clamp_int(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static uint32_t mr_stress_hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static gfx_color_t mr_stress_color(int r, int g, int b) {
  return GFX_RGB565(r, g, b);
}

static int mr_stress_abs_int(int v) { return v < 0 ? -v : v; }

static int mr_stress_tile_index(int v) {
#if MR_STRESS_TILE_SIZE == 16
  return v >> 4;
#elif MR_STRESS_TILE_SIZE == 8
  return v >> 3;
#elif MR_STRESS_TILE_SIZE == 32
  return v >> 5;
#else
  return v / MR_STRESS_TILE_SIZE;
#endif
}

static int mr_stress_hud_h(const mr_stress_test_t GFX_PTR *st) {
  if (!st)
    return 0;
  if ((st->cfg.features & MR_STRESS_FEATURE_HUD) == 0)
    return 0;
#if MR_STRESS_HUD_MODE <= 0
  return 0;
#elif MR_STRESS_HUD_MODE == 2
  if (st->cfg.screen_h <= 16 + 8)
    return 0;
  return 16;
#else
  if (st->cfg.screen_h <= MR_STRESS_HUD_H + 8)
    return 0;
  return MR_STRESS_HUD_H;
#endif
}

static int mr_stress_play_h(const mr_stress_test_t GFX_PTR *st) {
  int h;

  if (!st)
    return 1;
  h = st->cfg.screen_h - mr_stress_hud_h(st);
  if (h < 1)
    h = 1;
  return h;
}

static void mr_stress_set_fixed_camera(mr_stress_test_t GFX_PTR *st) {
  int max_camera_x;
  int max_camera_y;

  if (!st)
    return;

  max_camera_x = st->world_w - st->cfg.screen_w;
  max_camera_y = st->world_h - mr_stress_play_h(st);
  if (max_camera_x < 0)
    max_camera_x = 0;
  if (max_camera_y < 0)
    max_camera_y = 0;

  st->camera_x = max_camera_x / 2;
  st->camera_y = max_camera_y / 2;
  st->camera_vx = 0;
  st->camera_vy = 0;
}

static void mr_stress_make_tiles(mr_stress_test_t GFX_PTR *st) {
  int t;
  int y;
  int x;

  for (t = 0; t < MR_STRESS_TILESET_COUNT; ++t) {
    gfx_color_t c0;
    gfx_color_t c1;
    gfx_color_t border;

    c0 = mr_stress_color(20 + t * 18, 24 + t * 11, 32 + t * 7);
    c1 = mr_stress_color(36 + t * 18, 40 + t * 11, 52 + t * 7);
    border = mr_stress_color(110 + t * 9, 110 + t * 7, 96 + t * 5);

    for (y = 0; y < MR_STRESS_TILE_SIZE; ++y) {
      for (x = 0; x < MR_STRESS_TILE_SIZE; ++x) {
        gfx_color_t c;

        c = (((x ^ y ^ (t * 3)) & 4) != 0) ? c0 : c1;
        if (t == 1 || t == 5) {
          if (x == 0 || y == 0 || x == MR_STRESS_TILE_SIZE - 1 ||
              y == MR_STRESS_TILE_SIZE - 1)
            c = border;
        }
        if (t == 2 && ((x + y) & 7) == 0)
          c = border;
        if (t == 3 && (x == y || x == MR_STRESS_TILE_SIZE - 1 - y))
          c = border;
        if (t == 6 && ((x | y) & 3) == 0)
          c = border;

        st->tile_pixels[t][y * MR_STRESS_TILE_SIZE + x] = c;
      }
    }

    st->tileset[t].width = MR_STRESS_TILE_SIZE;
    st->tileset[t].height = MR_STRESS_TILE_SIZE;
    st->tileset[t].pixels = st->tile_pixels[t];
    st->tileset[t].runs = 0;
    st->tileset[t].run_count = 0;
#if defined(GFX_SPRITE_RLE_ROWSTART)
    st->tileset[t].row_start = 0;
#endif
    st->tileset[t].key = GFX_RGB565_BLACK;
    st->tileset[t].flags = 0;
  }
}

static void mr_stress_make_map(mr_stress_test_t GFX_PTR *st) {
  int y;
  int x;

  for (y = 0; y < MR_STRESS_MAP_H; ++y) {
    for (x = 0; x < MR_STRESS_MAP_W; ++x) {
      int idx;
      int solid;
      uint32_t h;
      unsigned tile;

      idx = y * MR_STRESS_MAP_W + x;
      h = mr_stress_hash((uint32_t)x * 73u + (uint32_t)y * 151u + 0x1234u);
      solid = 0;

      if (x == 0 || y == 0 || x == MR_STRESS_MAP_W - 1 ||
          y == MR_STRESS_MAP_H - 1)
        solid = 1;
      if (((x * 5 + y * 3) & 31) == 0)
        solid = 1;
      if (((x + y * 2) % 23) == 5 && (x & 3) != 0)
        solid = 1;

      tile = (unsigned)(h & 7u);
      if (solid)
        tile = 1u + ((unsigned)(x + y) & 1u) * 4u;

      st->tile_ids[idx] = (uint16_t)tile;
      st->solid[idx] = (uint8_t)solid;
    }
  }

  st->tilemap.map_w = MR_STRESS_MAP_W;
  st->tilemap.map_h = MR_STRESS_MAP_H;
  st->tilemap.tile_w = MR_STRESS_TILE_SIZE;
  st->tilemap.tile_h = MR_STRESS_TILE_SIZE;
  st->tilemap.tiles = st->tile_ids;
  st->tilemap.tileset = st->tileset;
  st->tilemap.tileset_count = MR_STRESS_TILESET_COUNT;
}

static void mr_stress_make_sprite(mr_stress_test_t GFX_PTR *st) {
  int x;
  int y;
  gfx_color_t key;

  key = GFX_RGB565_BLACK;

  for (y = 0; y < MR_STRESS_SPRITE_H; ++y) {
    for (x = 0; x < MR_STRESS_SPRITE_W; ++x) {
      int cx;
      int cy;
      int dist;
      gfx_color_t c;

      cx = x - (MR_STRESS_SPRITE_W / 2);
      cy = y - (MR_STRESS_SPRITE_H / 2);
      dist = mr_stress_abs_int(cx) + mr_stress_abs_int(cy);

      c = key;
      if (dist <= 8) {
        if (((x + y) & 3) == 0)
          c = mr_stress_color(255, 230, 80);
        else if (((x ^ y) & 2) == 0)
          c = mr_stress_color(250, 120, 40);
        else
          c = mr_stress_color(210, 40, 80);
      }
      if ((x == 7 || x == 8) && y >= 2 && y <= 13)
        c = mr_stress_color(255, 255, 255);

      st->sprite_source[y * MR_STRESS_SPRITE_W + x] = c;
    }
  }

#if defined(GFX_SPRITE_RLE_ROWSTART)
  gfx_rle_build_from_colorkey_indexed(
      st->sprite_source, MR_STRESS_SPRITE_W, MR_STRESS_SPRITE_H, key,
      st->sprite_runs, MR_STRESS_MAX_RLE_RUNS, st->sprite_pool,
      MR_STRESS_MAX_RLE_PIXELS, st->sprite_row_start, MR_STRESS_SPRITE_H + 1,
      &st->sprite);
#else
  gfx_rle_build_from_colorkey(st->sprite_source, MR_STRESS_SPRITE_W,
                              MR_STRESS_SPRITE_H, key, st->sprite_runs,
                              MR_STRESS_MAX_RLE_RUNS, st->sprite_pool,
                              MR_STRESS_MAX_RLE_PIXELS, &st->sprite);
#endif
}

static int mr_stress_rect_solid(mr_stress_test_t GFX_PTR *st, int x, int y,
                                int w, int h) {
  st->metrics.collision_checks++;

  if (x < 0 || y < 0 || x + w > st->world_w || y + h > st->world_h) {
    st->metrics.collision_hits++;
    return 1;
  }

#if MR_STRESS_TILE_SIZE == 16 && MR_STRESS_MAP_W == 32 &&                    \
    MR_STRESS_SPRITE_W == 16 && MR_STRESS_SPRITE_H == 16
  if (w == 16 && h == 16) {
    int tx0 = x >> 4;
    int ty0 = y >> 4;
    int tx1 = (x + 15) >> 4;
    int ty1 = (y + 15) >> 4;
    const uint8_t GFX_PTR *solid = st->solid;
    int row0 = ty0 << 5;

    if (solid[row0 + tx0] != 0 || solid[row0 + tx1] != 0) {
      st->metrics.collision_hits++;
      return 1;
    }
    if (ty1 != ty0) {
      int row1 = ty1 << 5;
      if (solid[row1 + tx0] != 0 || solid[row1 + tx1] != 0) {
        st->metrics.collision_hits++;
        return 1;
      }
    }
    return 0;
  }
#endif

  {
    int tx0;
    int ty0;
    int tx1;
    int ty1;
    int tx;
    int ty;

    tx0 = mr_stress_tile_index(x);
    ty0 = mr_stress_tile_index(y);
    tx1 = mr_stress_tile_index(x + w - 1);
    ty1 = mr_stress_tile_index(y + h - 1);

    tx0 = mr_stress_clamp_int(tx0, 0, MR_STRESS_MAP_W - 1);
    ty0 = mr_stress_clamp_int(ty0, 0, MR_STRESS_MAP_H - 1);
    tx1 = mr_stress_clamp_int(tx1, 0, MR_STRESS_MAP_W - 1);
    ty1 = mr_stress_clamp_int(ty1, 0, MR_STRESS_MAP_H - 1);

    for (ty = ty0; ty <= ty1; ++ty) {
      for (tx = tx0; tx <= tx1; ++tx) {
        if (st->solid[ty * MR_STRESS_MAP_W + tx] != 0) {
          st->metrics.collision_hits++;
          return 1;
        }
      }
    }
  }

  return 0;
}

static void mr_stress_place_actors(mr_stress_test_t GFX_PTR *st) {
  int i;
  int count;

  count = st->cfg.sprite_count;
  if (count > MR_STRESS_MAX_SPRITES)
    count = MR_STRESS_MAX_SPRITES;

  for (i = 0; i < count; ++i) {
    uint32_t h;
    int x;
    int y;
    int tries;

    h = mr_stress_hash((uint32_t)i * 2654435761u + 0x9e3779b9u);
    x = 16 + (int)(h % (uint32_t)(st->world_w - 48));
    h = mr_stress_hash(h + 0x51ed270bu);
    y = 16 + (int)(h % (uint32_t)(st->world_h - 48));

    for (tries = 0; tries < 32; ++tries) {
      if (!mr_stress_rect_solid(st, x, y, MR_STRESS_SPRITE_W,
                                MR_STRESS_SPRITE_H))
        break;
      x += 23;
      y += 37;
      if (x >= st->world_w - 32)
        x = 16 + (x % 31);
      if (y >= st->world_h - 32)
        y = 16 + (y % 29);
    }

    st->actors[i].x = x;
    st->actors[i].y = y;
    st->actors[i].old_x = x;
    st->actors[i].old_y = y;
    st->actors[i].vx = (int)((mr_stress_hash(h) & 3u) + 1u);
    st->actors[i].vy = (int)(((mr_stress_hash(h + 17u) >> 3) & 3u) + 1u);
    if ((i & 1) != 0)
      st->actors[i].vx = -st->actors[i].vx;
    if ((i & 2) != 0)
      st->actors[i].vy = -st->actors[i].vy;
    st->actors[i].phase = (uint8_t)(i & 255);
  }

  for (; i < MR_STRESS_MAX_SPRITES; ++i) {
    st->actors[i].x = 0;
    st->actors[i].y = 0;
    st->actors[i].old_x = 0;
    st->actors[i].old_y = 0;
    st->actors[i].vx = 0;
    st->actors[i].vy = 0;
    st->actors[i].phase = 0;
  }
}

void mr_stress_config_defaults(mr_stress_config_t GFX_PTR *cfg, int screen_w,
                               int screen_h) {
  if (!cfg)
    return;
  cfg->screen_w = screen_w;
  cfg->screen_h = screen_h;
  cfg->sprite_count = MR_STRESS_DEFAULT_SPRITES;
  cfg->target_fps = 120;
  cfg->features = MR_STRESS_FEATURE_DEFAULT;
  cfg->stats_sample_rate = 8;
}

void mr_stress_set_sprite_count(mr_stress_test_t GFX_PTR *st,
                                int sprite_count) {
  if (!st)
    return;
  if (sprite_count < 1)
    sprite_count = 1;
  if (sprite_count > MR_STRESS_MAX_SPRITES)
    sprite_count = MR_STRESS_MAX_SPRITES;
  st->cfg.sprite_count = sprite_count;
}

void mr_stress_init(mr_stress_test_t GFX_PTR *st,
                    const mr_stress_config_t GFX_PTR *cfg) {
  mr_stress_config_t local_cfg;

  if (!st)
    return;

  memset(st, 0, sizeof(*st));

  if (cfg)
    local_cfg = *cfg;
  else
    mr_stress_config_defaults(&local_cfg, 320, 240);

  if (local_cfg.screen_w <= 0)
    local_cfg.screen_w = 320;
  if (local_cfg.screen_h <= 0)
    local_cfg.screen_h = 200;
  if (local_cfg.sprite_count <= 0)
    local_cfg.sprite_count = MR_STRESS_DEFAULT_SPRITES;
  if (local_cfg.sprite_count > MR_STRESS_MAX_SPRITES)
    local_cfg.sprite_count = MR_STRESS_MAX_SPRITES;
  if (local_cfg.target_fps <= 0)
    local_cfg.target_fps = 120;
  if (local_cfg.features == 0)
    local_cfg.features = MR_STRESS_FEATURE_DEFAULT;
#if !MR_STRESS_ENABLE_TRIANGLES
  local_cfg.features &= ~MR_STRESS_FEATURE_TRIANGLES;
#endif
  if (local_cfg.stats_sample_rate <= 0)
    local_cfg.stats_sample_rate = 8;

  st->cfg = local_cfg;
  st->world_w = MR_STRESS_MAP_W * MR_STRESS_TILE_SIZE;
  st->world_h = MR_STRESS_MAP_H * MR_STRESS_TILE_SIZE;
  st->camera_x = 0;
  st->camera_y = 0;
  st->camera_vx = 1;
  st->camera_vy = 1;

  mr_stress_make_tiles(st);
  mr_stress_make_map(st);
  mr_stress_make_sprite(st);
  mr_stress_place_actors(st);
  if (MR_STRESS_FIXED_CAMERA)
    mr_stress_set_fixed_camera(st);

  st->metrics.frame = 0;
  st->metrics.sprite_count = (unsigned long)st->cfg.sprite_count;
  st->metrics.collision_checks = 0;
  st->metrics.collision_hits = 0;
  st->metrics.sprites_visible = 0;
  st->metrics.bucket_items = 0;
  st->metrics.fps10 = 0;
  st->metrics.avg_fps10 = 0;
  st->metrics.stats_sample_rate = (unsigned long)st->cfg.stats_sample_rate;
  st->metrics.stats_sampled = 0;
  st->bucket_frame = 0ul;
  st->bucket_tile_h = 0;
  st->bucket_band_count = 0;
  st->bucket_total_items = 0;
}

static void mr_stress_move_actor(mr_stress_test_t GFX_PTR *st,
                                 mr_stress_actor_t GFX_PTR *a) {
  int nx;
  int ny;
  int blocked;

  a->old_x = a->x;
  a->old_y = a->y;

  nx = a->x + a->vx;
  blocked = 0;
  if ((st->cfg.features & MR_STRESS_FEATURE_COLLISION) != 0) {
    if (mr_stress_rect_solid(st, nx, a->y, MR_STRESS_SPRITE_W,
                             MR_STRESS_SPRITE_H))
      blocked = 1;
  } else if (nx < 0 || nx + MR_STRESS_SPRITE_W >= st->world_w) {
    blocked = 1;
  }

  if (blocked) {
    a->vx = -a->vx;
    nx = a->x + a->vx;
  }
  a->x = nx;

  ny = a->y + a->vy;
  blocked = 0;
  if ((st->cfg.features & MR_STRESS_FEATURE_COLLISION) != 0) {
    if (mr_stress_rect_solid(st, a->x, ny, MR_STRESS_SPRITE_W,
                             MR_STRESS_SPRITE_H))
      blocked = 1;
  } else if (ny < 0 || ny + MR_STRESS_SPRITE_H >= st->world_h) {
    blocked = 1;
  }

  if (blocked) {
    a->vy = -a->vy;
    ny = a->y + a->vy;
  }
  a->y = ny;

  if (a->x < 1) {
    a->x = 1;
    a->vx = mr_stress_abs_int(a->vx);
  }
  if (a->y < 1) {
    a->y = 1;
    a->vy = mr_stress_abs_int(a->vy);
  }
  if (a->x > st->world_w - MR_STRESS_SPRITE_W - 1) {
    a->x = st->world_w - MR_STRESS_SPRITE_W - 1;
    a->vx = -mr_stress_abs_int(a->vx);
  }
  if (a->y > st->world_h - MR_STRESS_SPRITE_H - 1) {
    a->y = st->world_h - MR_STRESS_SPRITE_H - 1;
    a->vy = -mr_stress_abs_int(a->vy);
  }
}

void mr_stress_tick(mr_stress_test_t GFX_PTR *st) {
  int i;
  int count;
  int max_camera_x;
  int max_camera_y;

  if (!st)
    return;

  st->metrics.frame++;
  st->metrics.sprite_count = (unsigned long)st->cfg.sprite_count;
  st->metrics.collision_checks = 0;
  st->metrics.collision_hits = 0;

  count = st->cfg.sprite_count;
  if (count > MR_STRESS_MAX_SPRITES)
    count = MR_STRESS_MAX_SPRITES;

  for (i = 0; i < count; ++i)
    mr_stress_move_actor(st, &st->actors[i]);

  max_camera_x = st->world_w - st->cfg.screen_w;
  max_camera_y = st->world_h - mr_stress_play_h(st);
  if (max_camera_x < 0)
    max_camera_x = 0;
  if (max_camera_y < 0)
    max_camera_y = 0;

  if (MR_STRESS_FIXED_CAMERA) {
    mr_stress_set_fixed_camera(st);
  } else {
    st->camera_x += st->camera_vx;
    st->camera_y += st->camera_vy;
    if (st->camera_x <= 0 || st->camera_x >= max_camera_x) {
      st->camera_vx = -st->camera_vx;
      st->camera_x = mr_stress_clamp_int(st->camera_x, 0, max_camera_x);
    }
    if (st->camera_y <= 0 || st->camera_y >= max_camera_y) {
      st->camera_vy = -st->camera_vy;
      st->camera_y = mr_stress_clamp_int(st->camera_y, 0, max_camera_y);
    }
  }
}

static void mr_stress_prepare_buckets(mr_stress_test_t GFX_PTR *st, int tile_h,
                                      int y_offset) {
  int i;
  int b;
  int count;
  int bands;
  int total;

  if (!st)
    return;
  if (tile_h <= 0)
    tile_h = MR_STRESS_RENDER_BAND_H;

  if (st->bucket_frame == st->metrics.frame && st->bucket_tile_h == tile_h &&
      st->bucket_band_count > 0)
    return;

  bands = (st->cfg.screen_h + tile_h - 1) / tile_h;
  if (bands < 1)
    bands = 1;
  if (bands > MR_STRESS_MAX_BANDS)
    bands = MR_STRESS_MAX_BANDS;

  for (b = 0; b < bands; ++b) {
    st->bucket_count[b] = 0;
    st->bucket_start[b] = (uint16_t)(b * MR_STRESS_BUCKET_BAND_CAP);
  }
  st->bucket_start[bands] = (uint16_t)(bands * MR_STRESS_BUCKET_BAND_CAP);

  count = st->cfg.sprite_count;
  if (count > MR_STRESS_MAX_SPRITES)
    count = MR_STRESS_MAX_SPRITES;

  st->metrics.sprites_visible = 0;
  total = 0;

  /*
   * One-pass fixed-slice bucketing.  The older version counted, prefix-summed,
   * then filled, which meant two full actor passes every frame.  This keeps a
   * bounded slice per render band and appends visible actors directly.
   *
   * The Pico 2 performance profile normally renders one full-height band
   * (MR_TILE_H >= MR_VIEW_H).  In that case every visible sprite belongs to
   * band 0, so avoid per-sprite divide/clamp/band loops entirely.
   */
  if (bands == 1) {
    for (i = 0; i < count; ++i) {
      int sx;
      int sy;
      int c;

      sx = st->actors[i].x - st->camera_x;
      sy = st->actors[i].y - st->camera_y + y_offset;
      st->actor_screen_x[i] = sx;
      st->actor_screen_y[i] = sy;

      if (sx >= st->cfg.screen_w || sy >= st->cfg.screen_h ||
          sx + MR_STRESS_SPRITE_W <= 0 || sy + MR_STRESS_SPRITE_H <= 0)
        continue;

      st->metrics.sprites_visible++;
      c = (int)st->bucket_count[0];
      if (c < MR_STRESS_BUCKET_BAND_CAP) {
        st->bucket_actor[c] = (uint16_t)i;
        st->bucket_count[0] = (uint16_t)(c + 1);
        ++total;
      }
    }
  } else {
    for (i = 0; i < count; ++i) {
      int sx;
      int sy;
      int y0;
      int y1;
      int band0;
      int band1;

      sx = st->actors[i].x - st->camera_x;
      sy = st->actors[i].y - st->camera_y + y_offset;
      st->actor_screen_x[i] = sx;
      st->actor_screen_y[i] = sy;

      if (sx >= st->cfg.screen_w || sy >= st->cfg.screen_h ||
          sx + MR_STRESS_SPRITE_W <= 0 || sy + MR_STRESS_SPRITE_H <= 0)
        continue;

      st->metrics.sprites_visible++;

      y0 = sy;
      y1 = sy + MR_STRESS_SPRITE_H - 1;
      if (y0 < 0)
        band0 = 0;
      else
        band0 = y0 / tile_h;
      if (y1 >= st->cfg.screen_h)
        band1 = bands - 1;
      else if (y1 < 0)
        band1 = 0;
      else
        band1 = y1 / tile_h;

      if (band0 < 0)
        band0 = 0;
      if (band1 >= bands)
        band1 = bands - 1;
      if (band0 > band1)
        continue;

      for (b = band0; b <= band1; ++b) {
        int c;
        int pos;
        c = (int)st->bucket_count[b];
        if (c < MR_STRESS_BUCKET_BAND_CAP) {
          pos = (int)st->bucket_start[b] + c;
          if (pos < MR_STRESS_MAX_BUCKET_ITEMS) {
            st->bucket_actor[pos] = (uint16_t)i;
            st->bucket_count[b] = (uint16_t)(c + 1);
            ++total;
          }
        }
      }
    }
  }

  st->bucket_frame = st->metrics.frame;
  st->bucket_tile_h = tile_h;
  st->bucket_band_count = bands;
  st->bucket_total_items = total;
  st->metrics.bucket_items = (unsigned long)total;
  st->metrics.sprites_drawn = 0;
  st->metrics.sprites_rejected = 0;
  st->metrics.stats_sample_rate = (unsigned long)st->cfg.stats_sample_rate;
}

static void mr_stress_draw_triangles(gfx_renderer_t GFX_PTR *r,
                                     mr_stress_test_t GFX_PTR *st) {
#if GFX_ENABLE_TRIANGLES
  int f;
  int x0;
  int y0;
  int hud_h;

  /*
   * Keep the triangle stress visually isolated.  The renderer still executes
   * filled-triangle rasterization every frame, but the test area is small and
   * boxed so any triangle artifact is obvious and cannot masquerade as
   * LCD tearing, tilemap corruption, or bottom-of-screen scanline noise.
   */
  f = (int)(st->metrics.frame & 15ul);
  hud_h = mr_stress_hud_h(st);
  x0 = st->cfg.screen_w - 58;
  y0 = st->cfg.screen_h - 38;
  if (x0 < 4)
    x0 = 4;
  if (y0 < hud_h + 4)
    y0 = hud_h + 4;

  gfx_fill_rect(r, x0, y0, 56, 36, mr_stress_color(4, 8, 16));
  gfx_draw_rect(r, x0, y0, 56, 36, mr_stress_color(80, 120, 180));

  gfx_fill_triangle(r, x0 + 5 + (f >> 3), y0 + 5, x0 + 27, y0 + 16 + (f & 3),
                    x0 + 8, y0 + 30, mr_stress_color(28, 96, 170));
  gfx_fill_triangle(r, x0 + 31, y0 + 6 + (f >> 2), x0 + 51 - (f >> 3), y0 + 17,
                    x0 + 39, y0 + 30 - (f & 1), mr_stress_color(130, 48, 172));
#else
  (void)r;
  (void)st;
#endif
}

static void mr_stress_draw_hud(gfx_renderer_t GFX_PTR *r,
                               mr_stress_test_t GFX_PTR *st) {
  char buf[80];
  int hud_h;

  hud_h = mr_stress_hud_h(st);
  if (hud_h <= 0)
    return;

  gfx_fill_rect(r, 0, 0, st->cfg.screen_w, hud_h, GFX_RGB565_BLACK);

#if MR_STRESS_HUD_MODE == 2
  /* Clean capture HUD: one line only.  The old second line changed every
   * frame and could tear on ILI9341 panels because there is no TE/vsync
   * synchronization. */
  mr_stress_format_hud_line(buf, (int)sizeof(buf), st, 0);
  gfx_draw_text5x7(r, 2, 4, buf, GFX_RGB565_WHITE, 1);
#else
  /* Compact 320px-safe HUD: 5x7 font is about 6 px/char. */
  mr_stress_format_hud_line(buf, (int)sizeof(buf), st, 1);
  gfx_draw_text5x7(r, 2, 2, buf, GFX_RGB565_WHITE, 1);

  mr_stress_format_hud_detail(buf, (int)sizeof(buf), st);
  gfx_draw_text5x7(r, 2, 12, buf, GFX_RGB565_WHITE, 1);
#endif
}

void mr_stress_render(gfx_renderer_t GFX_PTR *r, mr_stress_test_t GFX_PTR *st) {
  int i;
  int hud_h;
  int play_h;
  int sample_stats;
  gfx_blit_stats_t stats;

  if (!r || !st)
    return;

  hud_h = mr_stress_hud_h(st);
  play_h = st->cfg.screen_h - hud_h;
  if (play_h < 1)
    play_h = st->cfg.screen_h;

  if ((st->cfg.features & MR_STRESS_FEATURE_TILEMAP) != 0) {
    gfx_draw_tilemap(r, &st->tilemap, st->camera_x, st->camera_y, 0, hud_h,
                     st->cfg.screen_w, play_h);
    st->metrics.tile_count_drawn =
        (unsigned long)(((st->cfg.screen_w + MR_STRESS_TILE_SIZE - 1) /
                             MR_STRESS_TILE_SIZE +
                         1) *
                        ((play_h + MR_STRESS_TILE_SIZE - 1) /
                             MR_STRESS_TILE_SIZE +
                         1));
  }

  if ((st->cfg.features & MR_STRESS_FEATURE_TRIANGLES) != 0)
    mr_stress_draw_triangles(r, st);

  mr_stress_prepare_buckets(st, r->tile_h, hud_h);

  sample_stats = 0;
#if !MR_STRESS_FAST_METRICS
  if ((st->cfg.features & MR_STRESS_FEATURE_STATS) != 0) {
    if (st->cfg.stats_sample_rate <= 1)
      sample_stats = 1;
    else if ((st->metrics.frame % (unsigned long)st->cfg.stats_sample_rate) ==
             0ul)
      sample_stats = 1;
  }

  if (sample_stats) {
    memset(&stats, 0, sizeof(stats));
    st->metrics.rle_runs_drawn = 0ul;
    st->metrics.rle_pixels_copied = 0ul;
    st->metrics.stats_sampled++;
  }
#endif

  {
    int band;
    int start;
    int end;

    band = (r->tile_h > 0) ? (r->tile_y / r->tile_h) : 0;
    if (band < 0 || band >= st->bucket_band_count) {
      start = 0;
      end = 0;
    } else {
      start = (int)st->bucket_start[band];
      end = start + (int)st->bucket_count[band];
    }

    for (i = start; i < end; ++i) {
      int actor_index;
      int sx;
      int sy;

      actor_index = (int)st->bucket_actor[i];
      sx = st->actor_screen_x[actor_index];
      sy = st->actor_screen_y[actor_index];
      if (sample_stats)
        gfx_blit_counted(r, &st->sprite, sx, sy, &stats);
      else {
        gfx_blit(r, &st->sprite, sx, sy);
#if !MR_STRESS_FAST_METRICS
        st->metrics.sprites_drawn++;
#endif
      }
    }
  }

  if (sample_stats) {
    st->metrics.sprites_drawn += stats.sprites_drawn;
    st->metrics.sprites_rejected += stats.sprites_rejected;
    st->metrics.rle_runs_drawn += stats.rle_runs_drawn;
    st->metrics.rle_pixels_copied += stats.rle_pixels_copied;
  }

  if ((st->cfg.features & MR_STRESS_FEATURE_HUD) != 0)
    mr_stress_draw_hud(r, st);
}

void mr_stress_get_metrics(const mr_stress_test_t GFX_PTR *st,
                           mr_stress_metrics_t GFX_PTR *out_metrics) {
  if (!st || !out_metrics)
    return;
  *out_metrics = st->metrics;
}

void mr_stress_set_fps10(mr_stress_test_t GFX_PTR *st, unsigned long fps10,
                         unsigned long avg_fps10) {
  if (!st)
    return;
  st->metrics.fps10 = fps10;
  st->metrics.avg_fps10 = avg_fps10;
}
