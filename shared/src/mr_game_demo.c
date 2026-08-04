#include "mr_game_demo.h"

#include <stdio.h>
#include <string.h>

#define MR_GAME_PLAYER_SPEED 3
#define MR_GAME_PLAYER_SLOW_SPEED 1
#define MR_GAME_DEADZONE_W 112
#define MR_GAME_DEADZONE_H 72
#define MR_GAME_SOLID_MASK GFX_TILE_SOLID
#define MR_GAME_TILE_SLOW 3
#define MR_GAME_TRIGGER_PICKUP 1
#define MR_GAME_TRIGGER_MESSAGE 2

static gfx_color_t mr_game_tile_color(int tile, int x, int y) {
  switch (tile) {
  case 0:
    return (((x ^ y) & 8) != 0) ? GFX_RGB565(24, 38, 24)
                                : GFX_RGB565(18, 30, 18);
  case 1:
    return (((x * 3 + y * 5) & 15) < 3) ? GFX_RGB565(40, 92, 35)
                                        : GFX_RGB565(30, 76, 30);
  case 2:
    return (x == 0 || y == 0 || x == 15 || y == 15) ? GFX_RGB565(150, 150, 165)
                                                    : GFX_RGB565(86, 86, 104);
  case 3:
    return (((x + y) & 7) < 3) ? GFX_RGB565(18, 72, 150)
                               : GFX_RGB565(10, 44, 112);
  case 4:
    return (((x >> 2) ^ (y >> 2)) & 1) ? GFX_RGB565(116, 82, 42)
                                       : GFX_RGB565(92, 64, 32);
  case 5:
    return (((x >> 3) ^ (y >> 3)) & 1) ? GFX_RGB565(52, 42, 72)
                                       : GFX_RGB565(32, 26, 48);
  case 6:
    return (((x + y) & 4) != 0) ? GFX_RGB565(130, 42, 28)
                                : GFX_RGB565(72, 22, 18);
  default:
    if ((x == 7 || x == 8) && (y >= 4 && y <= 11))
      return GFX_RGB565(220, 220, 60);
    if ((y == 7 || y == 8) && (x >= 4 && x <= 11))
      return GFX_RGB565(220, 220, 60);
    return GFX_RGB565(28, 72, 35);
  }
}

static void mr_game_make_tiles(mr_game_demo_t *demo) {
  int t;
  int x;
  int y;

  for (t = 0; t < MR_GAME_TILE_COUNT; ++t) {
    for (y = 0; y < MR_GAME_TILE_SIZE; ++y) {
      for (x = 0; x < MR_GAME_TILE_SIZE; ++x) {
        demo->tile_pixels[t][y * MR_GAME_TILE_SIZE + x] =
            mr_game_tile_color(t, x, y);
      }
    }
    demo->tile_sprites[t].width = MR_GAME_TILE_SIZE;
    demo->tile_sprites[t].height = MR_GAME_TILE_SIZE;
    demo->tile_sprites[t].pixels = demo->tile_pixels[t];
    demo->tile_sprites[t].runs = 0;
    demo->tile_sprites[t].run_count = 0;
    demo->tile_sprites[t].key = GFX_RGB565_BLACK;
    demo->tile_sprites[t].flags = 0u;
  }
}

static void mr_game_make_map(mr_game_demo_t *demo) {
  int x;
  int y;
  int tile;
  int solid;

  memset(demo->collision_flags, 0, sizeof(demo->collision_flags));

  for (y = 0; y < MR_GAME_MAP_H; ++y) {
    for (x = 0; x < MR_GAME_MAP_W; ++x) {
      solid = 0;
      tile = 0;

      if (x == 0 || y == 0 || x == MR_GAME_MAP_W - 1 ||
          y == MR_GAME_MAP_H - 1) {
        tile = 2;
        solid = 1;
      } else if (((x == 11 || x == 12) && y > 7 && y < 38) ||
                 ((y == 24 || y == 25) && x > 18 && x < 52)) {
        tile = 2;
        solid = 1;
      } else if (x > 36 && x < 48 && y > 8 && y < 17) {
        tile = 3;
      } else if (((x + y) % 17) == 0) {
        tile = 7;
      } else if ((x > 2 && x < 58 && (y == 6 || y == 7)) ||
                 (y > 2 && y < 54 && (x == 6 || x == 7))) {
        tile = 4;
      } else if (((x * 5 + y * 3) & 31) < 3) {
        tile = 1;
      } else if (((x ^ y) & 15) == 5) {
        tile = 5;
      }

      demo->tile_map[y * MR_GAME_MAP_W + x] = (uint16_t)tile;
      if (solid)
        demo->collision_flags[y * MR_GAME_MAP_W + x] = GFX_TILE_SOLID;
    }
  }

  demo->tilemap.map_w = MR_GAME_MAP_W;
  demo->tilemap.map_h = MR_GAME_MAP_H;
  demo->tilemap.tile_w = MR_GAME_TILE_SIZE;
  demo->tilemap.tile_h = MR_GAME_TILE_SIZE;
  demo->tilemap.tiles = demo->tile_map;
  demo->tilemap.tileset = demo->tile_sprites;
  demo->tilemap.tileset_count = MR_GAME_TILE_COUNT;

  gfx_collision_init(&demo->collision, MR_GAME_MAP_W, MR_GAME_MAP_H,
                     MR_GAME_TILE_SIZE, MR_GAME_TILE_SIZE,
                     demo->collision_flags, MR_GAME_SOLID_MASK);
}

static void mr_game_make_player_sprite(mr_game_demo_t *demo, int frame) {
  gfx_color_t *pixels;
  int x;
  int y;
  int step;

  pixels = frame ? demo->player_frame1_pixels : demo->player_frame0_pixels;
  step = frame ? 1 : 0;

  for (y = 0; y < 16; ++y) {
    for (x = 0; x < 16; ++x) {
      gfx_color_t c;
      c = GFX_RGB565_BLACK;
      if (x >= 4 && x <= 11 && y >= 2 && y <= 5)
        c = GFX_RGB565(240, 214, 160);
      if (x >= 5 && x <= 10 && y >= 6 && y <= 11)
        c = GFX_RGB565(40, 120, 230);
      if ((x == 5 || x == 10) && y >= 12 && y <= 15)
        c = GFX_RGB565(30, 30, 45);
      if (step && ((x == 4 && y >= 12) || (x == 11 && y >= 10)))
        c = GFX_RGB565(30, 30, 45);
      if ((x == 5 || x == 10) && y == 4)
        c = GFX_RGB565_BLACK;
      pixels[y * 16 + x] = c;
    }
  }

  demo->player_sprites[frame].width = 16;
  demo->player_sprites[frame].height = 16;
  demo->player_sprites[frame].pixels = pixels;
  demo->player_sprites[frame].runs = 0;
  demo->player_sprites[frame].run_count = 0;
  demo->player_sprites[frame].key = GFX_RGB565_BLACK;
  demo->player_sprites[frame].flags = GFX_SPRITE_COLORKEY;
}

static void mr_game_make_enemy_sprite(mr_game_demo_t *demo) {
  int x;
  int y;

  for (y = 0; y < 16; ++y) {
    for (x = 0; x < 16; ++x) {
      gfx_color_t c;
      c = GFX_RGB565_BLACK;
      if (x >= 2 && x <= 13 && y >= 4 && y <= 13)
        c = GFX_RGB565(180, 42, 60);
      if ((x == 4 || x == 11) && y == 7)
        c = GFX_RGB565_WHITE;
      if ((x == 5 || x == 12) && y == 7)
        c = GFX_RGB565_BLACK;
      if (y == 3 && (x == 4 || x == 11))
        c = GFX_RGB565(250, 190, 50);
      demo->enemy_pixels[y * 16 + x] = c;
    }
  }

  demo->enemy_sprite.width = 16;
  demo->enemy_sprite.height = 16;
  demo->enemy_sprite.pixels = demo->enemy_pixels;
  demo->enemy_sprite.runs = 0;
  demo->enemy_sprite.run_count = 0;
  demo->enemy_sprite.key = GFX_RGB565_BLACK;
  demo->enemy_sprite.flags = GFX_SPRITE_COLORKEY;
}

static void mr_game_make_pickup_sprite(mr_game_demo_t *demo) {
  int x;
  int y;

  for (y = 0; y < 12; ++y) {
    for (x = 0; x < 12; ++x) {
      gfx_color_t c;
      c = GFX_RGB565_BLACK;
      if (x >= 3 && x <= 8 && y >= 3 && y <= 8)
        c = GFX_RGB565_YELLOW;
      if (x == 5 || x == 6 || y == 5 || y == 6)
        c = GFX_RGB565_WHITE;
      demo->pickup_pixels[y * 12 + x] = c;
    }
  }

  demo->pickup_sprite.width = 12;
  demo->pickup_sprite.height = 12;
  demo->pickup_sprite.pixels = demo->pickup_pixels;
  demo->pickup_sprite.runs = 0;
  demo->pickup_sprite.run_count = 0;
  demo->pickup_sprite.key = GFX_RGB565_BLACK;
  demo->pickup_sprite.flags = GFX_SPRITE_COLORKEY;
}

static void mr_game_make_sprites(mr_game_demo_t *demo) {
  mr_game_make_player_sprite(demo, 0);
  mr_game_make_player_sprite(demo, 1);
  mr_game_make_enemy_sprite(demo);
  mr_game_make_pickup_sprite(demo);

  demo->player_anim_frames[0].sprite = &demo->player_sprites[0];
  demo->player_anim_frames[0].ticks = 8u;
  demo->player_anim_frames[1].sprite = &demo->player_sprites[1];
  demo->player_anim_frames[1].ticks = 8u;
  demo->player_anim.frames = demo->player_anim_frames;
  demo->player_anim.frame_count = 2;
  demo->player_anim.loop = 1u;
  demo->player_anim.pingpong = 0u;
}

static void mr_game_set_message(mr_game_demo_t *demo, const char *msg,
                                int ticks) {
  size_t n;

  if (!msg)
    msg = "";
  n = strlen(msg);
  if (n >= sizeof(demo->message))
    n = sizeof(demo->message) - 1u;
  memcpy(demo->message, msg, n);
  demo->message[n] = '\0';
  demo->message_timer = ticks;
}


static void mr_game_spawn_particle(mr_game_demo_t *demo, int world_x,
                                   int world_y, int vx8, int vy8, int life,
                                   gfx_color_t color) {
  mr_game_particle_t *p;

  if (!demo || life <= 0)
    return;
  p = &demo->particles[demo->particle_cursor];
  demo->particle_cursor = (demo->particle_cursor + 1) % MR_GAME_MAX_PARTICLES;
  p->x8 = world_x * 8;
  p->y8 = world_y * 8;
  p->vx8 = vx8;
  p->vy8 = vy8;
  p->life = life;
  p->color = color;
}

static void mr_game_spawn_burst(mr_game_demo_t *demo, int world_x, int world_y,
                                gfx_color_t color, int count) {
  static const signed char velocity[16][2] = {
      {-12, -20}, {-5, -23}, {5, -23},  {12, -20}, {18, -10}, {20, 0},
      {18, 10},   {12, 17},  {5, 20},   {-5, 20},  {-12, 17}, {-18, 10},
      {-20, 0},   {-18, -10},{0, -27},  {0, 23}};
  int i;

  if (count > 16)
    count = 16;
  for (i = 0; i < count; ++i) {
    mr_game_spawn_particle(demo, world_x, world_y, velocity[i][0],
                           velocity[i][1], 18 + (i & 7), color);
  }
}

static void mr_game_update_particles(mr_game_demo_t *demo) {
  int i;
  for (i = 0; i < MR_GAME_MAX_PARTICLES; ++i) {
    mr_game_particle_t *p = &demo->particles[i];
    if (p->life <= 0)
      continue;
    p->x8 += p->vx8;
    p->y8 += p->vy8;
    p->vy8 += 2;
    --p->life;
  }
}

void mr_game_demo_set_fps10(mr_game_demo_t *demo, unsigned long fps10,
                            unsigned long avg_fps10) {
  if (!demo)
    return;
  demo->fps10 = fps10;
  demo->avg_fps10 = avg_fps10;
}

static void mr_game_make_world_objects(mr_game_demo_t *demo) {
  int i;
  static const int enemy_pos[5][2] = {
      {220, 96}, {410, 170}, {650, 260}, {760, 620}, {290, 720}};
  /* Each 12x12 pickup is centred inside one verified walkable 16x16 tile.
     Pickup five intentionally demonstrates that the blue terrain is slow,
     not solid. */
  static const int pickup_pos[10][2] = {
      {130, 98},  {242, 130}, {386, 114}, {562, 226}, {706, 162},
      {210, 418}, {434, 418}, {610, 514}, {834, 418}, {754, 738}};

  demo->actor_count = 0;
  demo->player_index = 0;

  gfx_actor_init(&demo->actors[0], &demo->player_sprites[0], 48, 48, 10, 1);
  gfx_actor_set_anim(&demo->actors[0], &demo->player_anim);
  /* Bounds are expressed as collider extents, not sprite-origin maxima. */
  gfx_actor_set_bounds(&demo->actors[0], 0, 0, MR_GAME_WORLD_W,
                       MR_GAME_WORLD_H);
  gfx_actor_set_collider(&demo->actors[0], 3, 8, 10, 8);
  demo->actors[0].flags = (uint8_t)(demo->actors[0].flags | GFX_ACTOR_ACTIVE |
                                    GFX_ACTOR_VISIBLE | GFX_ACTOR_SOLID);
  demo->actor_count = 1;

  for (i = 0; i < 5; ++i) {
    gfx_actor_init(&demo->actors[demo->actor_count], &demo->enemy_sprite,
                   enemy_pos[i][0], enemy_pos[i][1], 8, 1);
    gfx_actor_set_bounds(&demo->actors[demo->actor_count], 0, 0,
                         MR_GAME_WORLD_W, MR_GAME_WORLD_H);
    gfx_actor_set_collider(&demo->actors[demo->actor_count], 2, 6, 12, 10);
    demo->actors[demo->actor_count].flags =
        (uint8_t)(demo->actors[demo->actor_count].flags | GFX_ACTOR_ACTIVE |
                  GFX_ACTOR_VISIBLE);
    ++demo->actor_count;
  }

  demo->pickup_count = MR_GAME_MAX_PICKUPS;
  for (i = 0; i < demo->pickup_count; ++i) {
    demo->pickups[i].world_x = pickup_pos[i][0];
    demo->pickups[i].world_y = pickup_pos[i][1];
    demo->pickups[i].taken = 0;
    demo->pickups[i].bob = i * 5;
  }

  demo->trigger_count = 2;
  demo->triggers[0].rect = gfx_rect_make(300, 80, 80, 60);
  demo->triggers[0].type = MR_GAME_TRIGGER_MESSAGE;
  demo->triggers[0].fired = 0;
  demo->triggers[0].message = "TRIGGER: SHARED GAME DEMO";
  demo->triggers[1].rect = gfx_rect_make(650, 650, 110, 70);
  demo->triggers[1].type = MR_GAME_TRIGGER_MESSAGE;
  demo->triggers[1].fired = 0;
  demo->triggers[1].message = "DOS + PICO SAME DEMO CORE";
}

static int mr_game_rects_overlap(gfx_rect_t a, gfx_rect_t b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
           b.y + b.h <= a.y);
}

static int mr_game_rect_overlaps_tile(const mr_game_demo_t *demo,
                                      gfx_rect_t rect, int wanted_tile) {
  int tx0;
  int ty0;
  int tx1;
  int ty1;
  int tx;
  int ty;

  if (!demo || rect.w <= 0 || rect.h <= 0)
    return 0;

  tx0 = rect.x / MR_GAME_TILE_SIZE;
  ty0 = rect.y / MR_GAME_TILE_SIZE;
  tx1 = (rect.x + rect.w - 1) / MR_GAME_TILE_SIZE;
  ty1 = (rect.y + rect.h - 1) / MR_GAME_TILE_SIZE;

  if (tx0 < 0)
    tx0 = 0;
  if (ty0 < 0)
    ty0 = 0;
  if (tx1 >= MR_GAME_MAP_W)
    tx1 = MR_GAME_MAP_W - 1;
  if (ty1 >= MR_GAME_MAP_H)
    ty1 = MR_GAME_MAP_H - 1;

  for (ty = ty0; ty <= ty1; ++ty) {
    for (tx = tx0; tx <= tx1; ++tx) {
      if ((int)demo->tile_map[ty * MR_GAME_MAP_W + tx] == wanted_tile)
        return 1;
    }
  }

  return 0;
}

static int mr_game_player_speed_for_move(const mr_game_demo_t *demo,
                                         const gfx_actor_t *player, int dx,
                                         int dy) {
  gfx_rect_t current;
  gfx_rect_t projected;

  if (!demo || !player)
    return MR_GAME_PLAYER_SPEED;

  current = gfx_actor_world_rect(player);
  projected = current;
  projected.x += dx * MR_GAME_PLAYER_SPEED;
  projected.y += dy * MR_GAME_PLAYER_SPEED;

  if (mr_game_rect_overlaps_tile(demo, current, MR_GAME_TILE_SLOW) ||
      mr_game_rect_overlaps_tile(demo, projected, MR_GAME_TILE_SLOW))
    return MR_GAME_PLAYER_SLOW_SPEED;

  return MR_GAME_PLAYER_SPEED;
}

static void mr_game_update_instances(mr_game_demo_t *demo) {
  int i;
  int n;
  int bob;

  n = 0;
  for (i = 0; i < demo->actor_count && n < MR_GAME_MAX_INSTANCES; ++i) {
    gfx_actor_sync_screen(&demo->actors[i], demo->camera.x, demo->camera.y);
    demo->instances[n] = demo->actors[i].inst;
    ++n;
  }

  for (i = 0; i < demo->pickup_count && n < MR_GAME_MAX_INSTANCES; ++i) {
    if (!demo->pickups[i].taken) {
      bob = ((int)((demo->frame + (unsigned long)demo->pickups[i].bob) & 31UL) <
             16)
                ? 0
                : 1;
      gfx_sprite_instance_init(&demo->instances[n], &demo->pickup_sprite,
                               demo->pickups[i].world_x - demo->camera.x,
                               demo->pickups[i].world_y - demo->camera.y + bob,
                               6, 1);
      ++n;
    }
  }

  demo->instance_count = n;
}

void mr_game_demo_init(mr_game_demo_t *demo, int screen_w, int screen_h) {
  if (!demo)
    return;

  memset(demo, 0, sizeof(*demo));
  demo->screen_w = screen_w;
  demo->screen_h = screen_h;

  mr_game_make_tiles(demo);
  mr_game_make_map(demo);
  mr_game_make_sprites(demo);
  mr_game_make_world_objects(demo);

  gfx_camera_init(&demo->camera, screen_w, screen_h, MR_GAME_WORLD_W,
                  MR_GAME_WORLD_H);
  gfx_camera_set_deadzone(&demo->camera, (screen_w - MR_GAME_DEADZONE_W) / 2,
                          (screen_h - MR_GAME_DEADZONE_H) / 2,
                          MR_GAME_DEADZONE_W, MR_GAME_DEADZONE_H);

  demo->last_dx = 1;
  demo->last_dy = 0;
  demo->title_screen = 1;
  demo->force_full_dirty = 1;
  mr_game_set_message(demo, "PRESS ENTER / AUTORUN STARTS",
                      MR_GAME_TICK_HZ * 2);
  mr_game_update_instances(demo);
}


void mr_game_demo_restart(mr_game_demo_t *demo) {
  unsigned long restarts;
  unsigned long fps10;
  unsigned long avg_fps10;
  int keep_debug;

  if (!demo)
    return;

  restarts = demo->restart_count + 1ul;
  fps10 = demo->fps10;
  avg_fps10 = demo->avg_fps10;
  keep_debug = demo->debug_overlay;

  mr_game_make_world_objects(demo);
  memset(demo->particles, 0, sizeof(demo->particles));
  demo->particle_cursor = 0;
  demo->frame = 0ul;
  demo->pickups_collected = 0ul;
  demo->trigger_hits = 0ul;
  demo->camera.x = 0;
  demo->camera.y = 0;
  demo->camera.shake_x = 0;
  demo->camera.shake_y = 0;
  demo->camera.shake_ticks = 0;
  demo->restart_count = restarts;
  demo->fps10 = fps10;
  demo->avg_fps10 = avg_fps10;
  demo->debug_overlay = keep_debug;
  demo->paused = 0;
  demo->title_screen = 0;
  demo->force_full_dirty = 1;
  demo->last_dx = 1;
  demo->last_dy = 0;
  demo->last_event = MR_GAME_EVENT_ENEMY_HIT;
  mr_game_set_message(demo, "ENEMY HIT - RESTART", MR_GAME_TICK_HZ * 2);
  mr_game_spawn_burst(demo, demo->actors[demo->player_index].world_x + 8,
                      demo->actors[demo->player_index].world_y + 8,
                      GFX_RGB565_RED, 16);
  mr_game_update_instances(demo);
}

static int mr_game_player_hits_enemy(const mr_game_demo_t *demo,
                                     gfx_rect_t player_rect) {
  int i;
  if (!demo)
    return 0;
  for (i = 1; i < demo->actor_count; ++i) {
    if ((demo->actors[i].flags & GFX_ACTOR_ACTIVE) != 0u &&
        mr_game_rects_overlap(player_rect,
                              gfx_actor_world_rect(&demo->actors[i])))
      return 1;
  }
  return 0;
}

static void mr_game_handle_triggers(mr_game_demo_t *demo,
                                    gfx_rect_t player_rect) {
  int i;

  for (i = 0; i < demo->trigger_count; ++i) {
    if (!demo->triggers[i].fired &&
        mr_game_rects_overlap(player_rect, demo->triggers[i].rect)) {
      demo->triggers[i].fired = 1;
      ++demo->trigger_hits;
      demo->last_event = MR_GAME_EVENT_MESSAGE;
      mr_game_set_message(demo, demo->triggers[i].message, MR_GAME_TICK_HZ * 3);
      gfx_camera_apply_shake(&demo->camera, 2, 1, 10);
    }
  }
}

static void mr_game_handle_pickups(mr_game_demo_t *demo,
                                   gfx_rect_t player_rect) {
  int i;
  gfx_rect_t pr;

  for (i = 0; i < demo->pickup_count; ++i) {
    if (!demo->pickups[i].taken) {
      pr = gfx_rect_make(demo->pickups[i].world_x, demo->pickups[i].world_y, 12,
                         12);
      if (mr_game_rects_overlap(player_rect, pr)) {
        demo->pickups[i].taken = 1;
        ++demo->pickups_collected;
        demo->last_event = MR_GAME_EVENT_PICKUP;
        mr_game_set_message(demo, "PICKUP", MR_GAME_TICK_HZ);
        mr_game_spawn_burst(demo, demo->pickups[i].world_x + 6,
                            demo->pickups[i].world_y + 6, GFX_RGB565_YELLOW, 10);
        gfx_camera_apply_shake(&demo->camera, 1, 1, 6);
      }
    }
  }
}

static void mr_game_update_enemies(mr_game_demo_t *demo) {
  int i;
  int dir;
  int speed;

  for (i = 1; i < demo->actor_count; ++i) {
    dir = (int)(((demo->frame / 70UL) + (unsigned long)i) & 3UL);
    speed = 1 + (i & 1);
    if (dir == 0)
      gfx_actor_move_collide(&demo->actors[i], &demo->collision, speed, 0);
    else if (dir == 1)
      gfx_actor_move_collide(&demo->actors[i], &demo->collision, 0, speed);
    else if (dir == 2)
      gfx_actor_move_collide(&demo->actors[i], &demo->collision, -speed, 0);
    else
      gfx_actor_move_collide(&demo->actors[i], &demo->collision, 0, -speed);
    gfx_actor_update_animation(&demo->actors[i]);
  }
}

void mr_game_demo_tick(mr_game_demo_t *demo, const mr_demo_input_t *input) {
  mr_demo_input_t zero_input;
  gfx_actor_t *player;
  gfx_rect_t before_rect;
  gfx_rect_t after_rect;
  int dx;
  int dy;
  int player_speed;

  if (!demo)
    return;

  memset(&zero_input, 0, sizeof(zero_input));
  if (!input)
    input = &zero_input;

  demo->last_event = MR_GAME_EVENT_NONE;

  if ((input->buttons & MR_DEMO_INPUT_QUIT) != 0u) {
    demo->quit_requested = 1;
    return;
  }

  if ((input->buttons & MR_DEMO_INPUT_DEBUG) != 0u) {
    demo->debug_overlay = !demo->debug_overlay;
    demo->force_full_dirty = 1;
  }

  if ((input->buttons & MR_DEMO_INPUT_PAUSE) != 0u) {
    demo->paused = !demo->paused;
    demo->force_full_dirty = 1;
  }

  if (demo->title_screen) {
    if ((input->buttons & (MR_DEMO_INPUT_START | MR_DEMO_INPUT_ACTION)) != 0u ||
        input->dx != 0 || input->dy != 0) {
      demo->title_screen = 0;
      demo->force_full_dirty = 1;
      mr_game_set_message(demo, "SHARED DEMO CORE", MR_GAME_TICK_HZ * 2);
    } else {
      ++demo->frame;
      mr_game_update_instances(demo);
      return;
    }
  }

  if (demo->paused) {
    ++demo->frame;
    mr_game_update_instances(demo);
    return;
  }

  player = &demo->actors[demo->player_index];
  dx = input->dx;
  dy = input->dy;
  if ((input->buttons & MR_DEMO_INPUT_LEFT) != 0u)
    dx = -1;
  if ((input->buttons & MR_DEMO_INPUT_RIGHT) != 0u)
    dx = 1;
  if ((input->buttons & MR_DEMO_INPUT_UP) != 0u)
    dy = -1;
  if ((input->buttons & MR_DEMO_INPUT_DOWN) != 0u)
    dy = 1;

  if (dx < -1)
    dx = -1;
  if (dx > 1)
    dx = 1;
  if (dy < -1)
    dy = -1;
  if (dy > 1)
    dy = 1;

  if (dx || dy) {
    demo->last_dx = dx;
    demo->last_dy = dy;
    player_speed = mr_game_player_speed_for_move(demo, player, dx, dy);
    gfx_actor_set_velocity(player, dx * player_speed, dy * player_speed);
  } else {
    gfx_actor_set_velocity(player, 0, 0);
  }

  before_rect = gfx_actor_world_rect(player);
  gfx_actor_move_collide(player, &demo->collision, player->vx, player->vy);
  after_rect = gfx_actor_world_rect(player);

  if ((dx || dy) && before_rect.x == after_rect.x &&
      before_rect.y == after_rect.y) {
    ++demo->collision_hits;
    demo->last_event = MR_GAME_EVENT_BLOCKED;
    gfx_camera_apply_shake(&demo->camera, 1, 1, 3);
  }

  gfx_actor_update_animation(player);
  mr_game_update_enemies(demo);

  after_rect = gfx_actor_world_rect(player);
  if (mr_game_player_hits_enemy(demo, after_rect)) {
    ++demo->collision_hits;
    mr_game_demo_restart(demo);
    mr_game_update_particles(demo);
    ++demo->frame;
    return;
  }

  mr_game_handle_pickups(demo, after_rect);
  mr_game_handle_triggers(demo, after_rect);
  mr_game_update_particles(demo);

  gfx_camera_follow_rect(&demo->camera, after_rect);
  gfx_camera_update_shake(&demo->camera);

  if (demo->message_timer > 0)
    --demo->message_timer;

  ++demo->frame;
  mr_game_update_instances(demo);
}

static void mr_game_draw_hud(mr_game_demo_t *demo, gfx_renderer_t *r) {
  char buf[80];

  gfx_fill_rect(r, 0, 0, demo->screen_w, 28, GFX_RGB565(0, 26, 64));
  gfx_draw_text5x7(r, 6, 5, "MICRORENDER SHARED GAME DEMO", GFX_RGB565_YELLOW,
                   1);

  sprintf(buf, "FPS=%lu.%lu AVG=%lu.%lu PICK=%lu/%d R=%lu",
          demo->fps10 / 10ul, demo->fps10 % 10ul, demo->avg_fps10 / 10ul,
          demo->avg_fps10 % 10ul, demo->pickups_collected, demo->pickup_count,
          demo->restart_count);
  gfx_draw_text5x7(r, 6, 17, buf, GFX_RGB565_WHITE, 1);

  if (demo->message_timer > 0 && demo->message[0]) {
    gfx_fill_rect(r, 6, demo->screen_h - 20, demo->screen_w - 12, 14,
                  GFX_RGB565(0, 0, 0));
    gfx_draw_rect(r, 6, demo->screen_h - 20, demo->screen_w - 12, 14,
                  GFX_RGB565_CYAN);
    gfx_draw_text5x7(r, 10, demo->screen_h - 17, demo->message, GFX_RGB565_CYAN,
                     1);
  }

  if (demo->debug_overlay) {
    sprintf(buf, "COL=%lu TRIG=%lu CAM=%d,%d P=%d,%d",
            demo->collision_hits, demo->trigger_hits, demo->camera.x,
            demo->camera.y, demo->actors[demo->player_index].world_x,
            demo->actors[demo->player_index].world_y);
    gfx_fill_rect(r, 0, 30, demo->screen_w, 12, GFX_RGB565(36, 0, 50));
    gfx_draw_text5x7(r, 6, 33, buf, GFX_RGB565_MAGENTA, 1);
  }
}

void mr_game_demo_render(mr_game_demo_t *demo, gfx_renderer_t *renderer) {
  int i;
  int tx;
  int ty;

  if (!demo || !renderer)
    return;

  gfx_fill_rect(renderer, 0, 0, demo->screen_w, demo->screen_h,
                GFX_RGB565_BLACK);
  gfx_draw_tilemap(renderer, &demo->tilemap, demo->camera.x, demo->camera.y, 0,
                   0, demo->screen_w, demo->screen_h);

  for (i = 0; i < demo->instance_count; ++i) {
    if (demo->instances[i].visible && demo->instances[i].sprite) {
      gfx_blit(renderer, demo->instances[i].sprite, demo->instances[i].x,
               demo->instances[i].y);
    }
  }


  for (i = 0; i < MR_GAME_MAX_PARTICLES; ++i) {
    mr_game_particle_t *p = &demo->particles[i];
    int px;
    int py;
    if (p->life <= 0)
      continue;
    px = p->x8 / 8 - demo->camera.x;
    py = p->y8 / 8 - demo->camera.y;
    if (px >= 0 && py >= 0 && px < demo->screen_w && py < demo->screen_h) {
      gfx_draw_pixel(renderer, px, py, p->color);
      if (p->life > 12 && px + 1 < demo->screen_w)
        gfx_draw_pixel(renderer, px + 1, py, p->color);
    }
  }

  for (i = 0; i < demo->trigger_count; ++i) {
    if (!demo->triggers[i].fired) {
      tx = demo->triggers[i].rect.x - demo->camera.x;
      ty = demo->triggers[i].rect.y - demo->camera.y;
      gfx_draw_rect(renderer, tx, ty, demo->triggers[i].rect.w,
                    demo->triggers[i].rect.h, GFX_RGB565_MAGENTA);
    }
  }

  mr_game_draw_hud(demo, renderer);

  if (demo->title_screen) {
    gfx_fill_rect(renderer, 24, 54, demo->screen_w - 48, 76,
                  GFX_RGB565(0, 0, 0));
    gfx_draw_rect(renderer, 24, 54, demo->screen_w - 48, 76, GFX_RGB565_YELLOW);
    gfx_draw_text5x7(renderer, 42, 72, "SHARED GAME DEMO", GFX_RGB565_YELLOW,
                     2);
    gfx_draw_text5x7(renderer, 44, 104, "DOS: KEYS   PICO: AUTORUN",
                     GFX_RGB565_WHITE, 1);
  }
}

int mr_game_demo_copy_instances(const mr_game_demo_t *demo,
                                gfx_sprite_instance_t *out_instances,
                                int max_instances) {
  int n;
  int i;

  if (!demo || !out_instances || max_instances <= 0)
    return 0;

  n = demo->instance_count;
  if (n > max_instances)
    n = max_instances;

  for (i = 0; i < n; ++i) {
    out_instances[i] = demo->instances[i];
  }

  return n;
}

const gfx_tilemap_t *mr_game_demo_tilemap(const mr_game_demo_t *demo) {
  if (!demo)
    return 0;
  return &demo->tilemap;
}

int mr_game_demo_quit_requested(const mr_game_demo_t *demo) {
  return demo ? demo->quit_requested : 1;
}
