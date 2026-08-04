#ifndef MR_GAME_DEMO_H
#define MR_GAME_DEMO_H

#include "gfx.h"
#include "gfx_engine.h"
#include "mr_demo_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MR_GAME_SCREEN_W_DEFAULT 320
#define MR_GAME_SCREEN_H_DEFAULT 240
#define MR_GAME_TILE_SIZE 16
#define MR_GAME_TILE_COUNT 8
#define MR_GAME_MAP_W 64
#define MR_GAME_MAP_H 64
#define MR_GAME_WORLD_W (MR_GAME_MAP_W * MR_GAME_TILE_SIZE)
#define MR_GAME_WORLD_H (MR_GAME_MAP_H * MR_GAME_TILE_SIZE)
#define MR_GAME_MAX_ACTORS 6
#define MR_GAME_MAX_PICKUPS 10
#define MR_GAME_MAX_INSTANCES (MR_GAME_MAX_ACTORS + MR_GAME_MAX_PICKUPS)
#define MR_GAME_MAX_PARTICLES 48
#define MR_GAME_TICK_HZ 70
#define MR_GAME_EVENT_NONE 0
#define MR_GAME_EVENT_PICKUP 1
#define MR_GAME_EVENT_MESSAGE 2
#define MR_GAME_EVENT_BLOCKED 3
#define MR_GAME_EVENT_ENEMY_HIT 4

typedef struct mr_game_pickup {
  int world_x;
  int world_y;
  int taken;
  int bob;
} mr_game_pickup_t;

typedef struct mr_game_trigger {
  gfx_rect_t rect;
  int type;
  int fired;
  const char *message;
} mr_game_trigger_t;

typedef struct mr_game_particle {
  /* Positions and velocities use eighth-pixel units so the implementation
     remains deterministic and floating-point-free on DOS and RP2350. */
  int x8;
  int y8;
  int vx8;
  int vy8;
  int life;
  gfx_color_t color;
} mr_game_particle_t;

typedef struct mr_game_demo {
  int screen_w;
  int screen_h;

  unsigned long frame;
  unsigned long collision_hits;
  unsigned long pickups_collected;
  unsigned long trigger_hits;
  unsigned long restart_count;
  unsigned long fps10;
  unsigned long avg_fps10;

  gfx_camera_t camera;
  gfx_collision_map_t collision;
  uint8_t collision_flags[MR_GAME_MAP_W * MR_GAME_MAP_H];

  gfx_color_t tile_pixels[MR_GAME_TILE_COUNT]
                         [MR_GAME_TILE_SIZE * MR_GAME_TILE_SIZE];
  gfx_sprite_t tile_sprites[MR_GAME_TILE_COUNT];
  uint16_t tile_map[MR_GAME_MAP_W * MR_GAME_MAP_H];
  gfx_tilemap_t tilemap;

  gfx_color_t player_frame0_pixels[16 * 16];
  gfx_color_t player_frame1_pixels[16 * 16];
  gfx_sprite_t player_sprites[2];
  gfx_anim_frame_t player_anim_frames[2];
  gfx_anim_t player_anim;

  gfx_color_t enemy_pixels[16 * 16];
  gfx_sprite_t enemy_sprite;

  gfx_color_t pickup_pixels[12 * 12];
  gfx_sprite_t pickup_sprite;

  gfx_actor_t actors[MR_GAME_MAX_ACTORS];
  int actor_count;

  mr_game_pickup_t pickups[MR_GAME_MAX_PICKUPS];
  int pickup_count;

  mr_game_trigger_t triggers[8];
  int trigger_count;

  gfx_sprite_instance_t instances[MR_GAME_MAX_INSTANCES];
  int instance_count;

  mr_game_particle_t particles[MR_GAME_MAX_PARTICLES];
  int particle_cursor;

  int player_index;
  int last_dx;
  int last_dy;
  int debug_overlay;
  int paused;
  int title_screen;
  int quit_requested;
  int force_full_dirty;
  int message_timer;
  int last_event;
  char message[48];
} mr_game_demo_t;

void mr_game_demo_init(mr_game_demo_t *demo, int screen_w, int screen_h);
void mr_game_demo_tick(mr_game_demo_t *demo, const mr_demo_input_t *input);
void mr_game_demo_render(mr_game_demo_t *demo, gfx_renderer_t *renderer);
void mr_game_demo_set_fps10(mr_game_demo_t *demo, unsigned long fps10,
                            unsigned long avg_fps10);
void mr_game_demo_restart(mr_game_demo_t *demo);
int mr_game_demo_copy_instances(const mr_game_demo_t *demo,
                                gfx_sprite_instance_t *out_instances,
                                int max_instances);
const gfx_tilemap_t *mr_game_demo_tilemap(const mr_game_demo_t *demo);
int mr_game_demo_quit_requested(const mr_game_demo_t *demo);

#ifdef __cplusplus
}
#endif

#endif /* MR_GAME_DEMO_H */
