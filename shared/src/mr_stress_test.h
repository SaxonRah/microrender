#ifndef MR_STRESS_TEST_H
#define MR_STRESS_TEST_H

#include "gfx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MR_STRESS_MAX_SPRITES
#define MR_STRESS_MAX_SPRITES 1024
#endif

#ifndef MR_STRESS_DEFAULT_SPRITES
#define MR_STRESS_DEFAULT_SPRITES 512
#endif

#ifndef MR_STRESS_MAX_BANDS
#define MR_STRESS_MAX_BANDS 16
#endif

#ifndef MR_STRESS_BUCKET_BAND_CAP
#define MR_STRESS_BUCKET_BAND_CAP 512
#endif

#ifndef MR_STRESS_FIXED_CAMERA
#define MR_STRESS_FIXED_CAMERA 0
#endif

#ifndef MR_STRESS_ENABLE_TRIANGLES
#define MR_STRESS_ENABLE_TRIANGLES 1
#endif

#ifndef MR_STRESS_MAX_BUCKET_ITEMS
#define MR_STRESS_MAX_BUCKET_ITEMS                                             \
  (MR_STRESS_MAX_BANDS * MR_STRESS_BUCKET_BAND_CAP)
#endif

#ifndef MR_STRESS_RENDER_BAND_H
#define MR_STRESS_RENDER_BAND_H 16
#endif

#ifndef MR_STRESS_CACHE_FIXED_BG
#define MR_STRESS_CACHE_FIXED_BG 0
#endif

#ifndef MR_STRESS_CACHE_MAX_W
#define MR_STRESS_CACHE_MAX_W 320
#endif

#ifndef MR_STRESS_CACHE_MAX_H
#define MR_STRESS_CACHE_MAX_H 240
#endif

#ifndef MR_STRESS_FAST_METRICS
#define MR_STRESS_FAST_METRICS 0
#endif

#ifndef MR_STRESS_MAP_W
#define MR_STRESS_MAP_W 32
#endif

#ifndef MR_STRESS_MAP_H
#define MR_STRESS_MAP_H 32
#endif

#ifndef MR_STRESS_TILE_SIZE
#define MR_STRESS_TILE_SIZE 16
#endif

#ifndef MR_STRESS_TILESET_COUNT
#define MR_STRESS_TILESET_COUNT 8
#endif

#ifndef MR_STRESS_HUD_H
#define MR_STRESS_HUD_H 24
#endif

#ifndef MR_STRESS_SPRITE_W
#define MR_STRESS_SPRITE_W 16
#endif

#ifndef MR_STRESS_SPRITE_H
#define MR_STRESS_SPRITE_H 16
#endif

#ifndef MR_STRESS_MAX_RLE_RUNS
#define MR_STRESS_MAX_RLE_RUNS 96
#endif

#ifndef MR_STRESS_MAX_RLE_PIXELS
#define MR_STRESS_MAX_RLE_PIXELS (MR_STRESS_SPRITE_W * MR_STRESS_SPRITE_H)
#endif

#define MR_STRESS_FEATURE_TILEMAP 0x0001u
#define MR_STRESS_FEATURE_COLLISION 0x0002u
#define MR_STRESS_FEATURE_TRIANGLES 0x0004u
#define MR_STRESS_FEATURE_STATS 0x0008u
#define MR_STRESS_FEATURE_HUD 0x0010u
#define MR_STRESS_FEATURE_DEFAULT                                              \
  (MR_STRESS_FEATURE_TILEMAP | MR_STRESS_FEATURE_COLLISION |                   \
   MR_STRESS_FEATURE_TRIANGLES | MR_STRESS_FEATURE_STATS |                     \
   MR_STRESS_FEATURE_HUD)

typedef struct mr_stress_config {
  int screen_w;
  int screen_h;
  int sprite_count;
  unsigned features;
  int stats_sample_rate;
} mr_stress_config_t;

typedef struct mr_stress_metrics {
  unsigned long frame;
  unsigned long sprite_count;
  unsigned long collision_checks;
  unsigned long collision_hits;
  unsigned long sprites_drawn;
  unsigned long sprites_rejected;
  unsigned long sprites_visible;
  unsigned long bucket_items;
  unsigned long rle_runs_drawn;
  unsigned long rle_pixels_copied;
  unsigned long tile_count_drawn;
  unsigned long fps10;
  unsigned long avg_fps10;
  unsigned long stats_sample_rate;
  unsigned long stats_sampled;
} mr_stress_metrics_t;

typedef struct mr_stress_actor {
  int x;
  int y;
  int old_x;
  int old_y;
  int vx;
  int vy;
  uint8_t phase;
} mr_stress_actor_t;

typedef struct mr_stress_test {
  mr_stress_config_t cfg;
  mr_stress_metrics_t metrics;

  int world_w;
  int world_h;
  int camera_x;
  int camera_y;
  int camera_vx;
  int camera_vy;

  gfx_tilemap_t tilemap;
  uint16_t tile_ids[MR_STRESS_MAP_W * MR_STRESS_MAP_H];
  uint8_t solid[MR_STRESS_MAP_W * MR_STRESS_MAP_H];

  gfx_sprite_t tileset[MR_STRESS_TILESET_COUNT];
  gfx_color_t tile_pixels[MR_STRESS_TILESET_COUNT]
                         [MR_STRESS_TILE_SIZE * MR_STRESS_TILE_SIZE];

  gfx_sprite_t sprite;
  gfx_color_t sprite_source[MR_STRESS_SPRITE_W * MR_STRESS_SPRITE_H];
  gfx_color_t sprite_pool[MR_STRESS_MAX_RLE_PIXELS];
  gfx_rle_run_t sprite_runs[MR_STRESS_MAX_RLE_RUNS];
#if defined(GFX_SPRITE_RLE_ROWSTART)
  uint16_t sprite_row_start[MR_STRESS_SPRITE_H + 1];
#else
  uint16_t sprite_row_start_unused[MR_STRESS_SPRITE_H + 1];
#endif

  mr_stress_actor_t actors[MR_STRESS_MAX_SPRITES];

  unsigned long bucket_frame;
  int bucket_tile_h;
  int bucket_band_count;
  int bucket_total_items;
  uint16_t bucket_start[MR_STRESS_MAX_BANDS + 1];
  uint16_t bucket_count[MR_STRESS_MAX_BANDS];
  uint16_t bucket_cursor[MR_STRESS_MAX_BANDS];
  uint16_t bucket_actor[MR_STRESS_MAX_BUCKET_ITEMS];
  int actor_screen_x[MR_STRESS_MAX_SPRITES];
  int actor_screen_y[MR_STRESS_MAX_SPRITES];

#if MR_STRESS_CACHE_FIXED_BG
  int bg_cache_valid;
  int bg_cache_w;
  int bg_cache_h;
  int bg_cache_camera_x;
  int bg_cache_camera_y;
  gfx_color_t bg_cache[MR_STRESS_CACHE_MAX_W * MR_STRESS_CACHE_MAX_H];
#endif
} mr_stress_test_t;

void mr_stress_config_defaults(mr_stress_config_t GFX_PTR *cfg, int screen_w,
                               int screen_h);
void mr_stress_init(mr_stress_test_t GFX_PTR *st,
                    const mr_stress_config_t GFX_PTR *cfg);
/* Advance one deterministic workload state. Benchmark frontends normally call
   this exactly once per rendered stress frame; it is deliberately not a
   wall-clock timestep. */
void mr_stress_tick(mr_stress_test_t GFX_PTR *st);
void mr_stress_render(gfx_renderer_t GFX_PTR *r, mr_stress_test_t GFX_PTR *st);
void mr_stress_get_metrics(const mr_stress_test_t GFX_PTR *st,
                           mr_stress_metrics_t GFX_PTR *out_metrics);
void mr_stress_set_fps10(mr_stress_test_t GFX_PTR *st, unsigned long fps10,
                         unsigned long avg_fps10);
void mr_stress_set_sprite_count(mr_stress_test_t GFX_PTR *st, int sprite_count);

#ifdef __cplusplus
}
#endif

#endif /* MR_STRESS_TEST_H */
