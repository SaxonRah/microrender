#include "mr_game_demo.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static int count_live_particles(const mr_game_demo_t *demo) {
  int i;
  int count = 0;
  for (i = 0; i < MR_GAME_MAX_PARTICLES; ++i) {
    if (demo->particles[i].life > 0)
      ++count;
  }
  return count;
}

static void test_dimensions_and_camera(void) {
  mr_game_demo_t demo;
  gfx_rect_t target;

  mr_game_demo_init(&demo, 320, 240);

  CHECK(demo.screen_w == 320);
  CHECK(demo.screen_h == 240);
  CHECK(demo.camera.screen_w == 320);
  CHECK(demo.camera.screen_h == 240);
  CHECK(demo.camera.map_w == MR_GAME_WORLD_W);
  CHECK(demo.camera.map_h == MR_GAME_WORLD_H);

  target = gfx_rect_make(MR_GAME_WORLD_W - 10, MR_GAME_WORLD_H - 8, 10, 8);
  gfx_camera_follow_rect(&demo.camera, target);
  CHECK(demo.camera.x == MR_GAME_WORLD_W - 320);
  CHECK(demo.camera.y == MR_GAME_WORLD_H - 240);

  target = gfx_rect_make(0, 0, 10, 8);
  gfx_camera_follow_rect(&demo.camera, target);
  CHECK(demo.camera.x == 0);
  CHECK(demo.camera.y == 0);
}

static void test_actor_bounds(void) {
  mr_game_demo_t demo;
  gfx_actor_t *player;
  gfx_rect_t rect;

  mr_game_demo_init(&demo, 320, 240);
  player = &demo.actors[demo.player_index];

  player->world_x = MR_GAME_WORLD_W - player->collider_x - player->collider_w;
  player->world_y = MR_GAME_WORLD_H - player->collider_y - player->collider_h;
  gfx_actor_move_collide(player, 0, 100, 100);
  rect = gfx_actor_world_rect(player);
  CHECK(rect.x + rect.w <= MR_GAME_WORLD_W);
  CHECK(rect.y + rect.h <= MR_GAME_WORLD_H);

  player->world_x = -player->collider_x;
  player->world_y = -player->collider_y;
  gfx_actor_move_collide(player, 0, -100, -100);
  rect = gfx_actor_world_rect(player);
  CHECK(rect.x >= 0);
  CHECK(rect.y >= 0);
}

static void test_pickup_particles(void) {
  mr_game_demo_t demo;
  mr_demo_input_t input;
  gfx_rect_t player_rect;

  memset(&input, 0, sizeof(input));
  mr_game_demo_init(&demo, 320, 240);
  demo.title_screen = 0;

  player_rect = gfx_actor_world_rect(&demo.actors[demo.player_index]);
  demo.pickups[0].world_x = player_rect.x;
  demo.pickups[0].world_y = player_rect.y;

  mr_game_demo_tick(&demo, &input);

  CHECK(demo.pickups[0].taken == 1);
  CHECK(demo.pickups_collected == 1ul);
  CHECK(demo.last_event == MR_GAME_EVENT_PICKUP);
  CHECK(count_live_particles(&demo) > 0);
}

static void test_enemy_restart_clears_pickups(void) {
  mr_game_demo_t demo;
  mr_demo_input_t input;
  gfx_actor_t *player;
  gfx_actor_t *enemy;
  int i;

  memset(&input, 0, sizeof(input));
  mr_game_demo_init(&demo, 320, 240);
  demo.title_screen = 0;
  demo.debug_overlay = 1;
  demo.frame = 777ul;
  mr_game_demo_set_fps10(&demo, 558ul, 551ul);

  for (i = 0; i < demo.pickup_count; ++i)
    demo.pickups[i].taken = 1;
  demo.pickups_collected = (unsigned long)demo.pickup_count;

  player = &demo.actors[demo.player_index];
  enemy = &demo.actors[1];
  enemy->world_x = player->world_x;
  enemy->world_y = player->world_y;
  enemy->inst.x = enemy->world_x;
  enemy->inst.y = enemy->world_y;

  mr_game_demo_tick(&demo, &input);

  CHECK(demo.restart_count == 1ul);
  CHECK(demo.frame == 1ul);
  CHECK(demo.last_event == MR_GAME_EVENT_ENEMY_HIT);
  CHECK(demo.pickups_collected == 0ul);
  CHECK(demo.actors[demo.player_index].world_x == 48);
  CHECK(demo.actors[demo.player_index].world_y == 48);
  CHECK(demo.camera.x == 0);
  CHECK(demo.camera.y == 0);
  CHECK(demo.title_screen == 0);
  CHECK(demo.debug_overlay == 1);
  CHECK(demo.fps10 == 558ul);
  CHECK(demo.avg_fps10 == 551ul);
  CHECK(count_live_particles(&demo) > 0);

  for (i = 0; i < demo.pickup_count; ++i)
    CHECK(demo.pickups[i].taken == 0);
}

static void test_no_double_move(void) {
  mr_game_demo_t demo;
  mr_demo_input_t input;
  int start_x;

  memset(&input, 0, sizeof(input));
  input.dx = 1;
  input.buttons = MR_DEMO_INPUT_RIGHT;

  mr_game_demo_init(&demo, 320, 240);
  demo.title_screen = 0;
  start_x = demo.actors[demo.player_index].world_x;

  mr_game_demo_tick(&demo, &input);
  CHECK(demo.actors[demo.player_index].world_x == start_x + 3);
}

static void test_slow_terrain_is_walkable(void) {
  mr_game_demo_t demo;
  mr_demo_input_t input;
  gfx_actor_t *player;
  int start_x;
  int slow_tx = 44;
  int slow_ty = 10;

  memset(&input, 0, sizeof(input));
  input.dx = 1;
  input.buttons = MR_DEMO_INPUT_RIGHT;

  mr_game_demo_init(&demo, 320, 240);
  demo.title_screen = 0;
  player = &demo.actors[demo.player_index];

  CHECK(demo.tile_map[slow_ty * MR_GAME_MAP_W + slow_tx] == 3u);
  CHECK(demo.collision_flags[slow_ty * MR_GAME_MAP_W + slow_tx] == 0u);

  /* Place the player's foot collider completely inside the blue field. */
  player->world_x = slow_tx * MR_GAME_TILE_SIZE + 2;
  player->world_y = slow_ty * MR_GAME_TILE_SIZE - player->collider_y;
  player->inst.x = player->world_x;
  player->inst.y = player->world_y;
  start_x = player->world_x;

  mr_game_demo_tick(&demo, &input);

  CHECK(demo.actors[demo.player_index].world_x == start_x + 1);
  CHECK(demo.last_event != MR_GAME_EVENT_BLOCKED);
}

static void test_grey_blocks_remain_solid(void) {
  mr_game_demo_t demo;
  mr_demo_input_t input;
  gfx_actor_t *player;
  int start_x;
  int wall_tx = 11;
  int wall_ty = 10;

  memset(&input, 0, sizeof(input));
  input.dx = 1;
  input.buttons = MR_DEMO_INPUT_RIGHT;

  mr_game_demo_init(&demo, 320, 240);
  demo.title_screen = 0;
  player = &demo.actors[demo.player_index];

  CHECK(demo.tile_map[wall_ty * MR_GAME_MAP_W + wall_tx] == 2u);
  CHECK(demo.collision_flags[wall_ty * MR_GAME_MAP_W + wall_tx] ==
        GFX_TILE_SOLID);

  /* Put the collider directly against the left edge of the grey wall. */
  player->world_x = wall_tx * MR_GAME_TILE_SIZE - player->collider_x -
                    player->collider_w;
  player->world_y = wall_ty * MR_GAME_TILE_SIZE - player->collider_y;
  player->inst.x = player->world_x;
  player->inst.y = player->world_y;
  start_x = player->world_x;

  mr_game_demo_tick(&demo, &input);

  CHECK(demo.actors[demo.player_index].world_x == start_x);
  CHECK(demo.last_event == MR_GAME_EVENT_BLOCKED);
}

static void test_pickups_are_on_walkable_tiles(void) {
  mr_game_demo_t demo;
  int i;

  mr_game_demo_init(&demo, 320, 240);

  for (i = 0; i < demo.pickup_count; ++i) {
    gfx_rect_t pickup_rect =
        gfx_rect_make(demo.pickups[i].world_x, demo.pickups[i].world_y, 12, 12);
    CHECK(pickup_rect.x >= 0);
    CHECK(pickup_rect.y >= 0);
    CHECK(pickup_rect.x + pickup_rect.w <= MR_GAME_WORLD_W);
    CHECK(pickup_rect.y + pickup_rect.h <= MR_GAME_WORLD_H);
    CHECK(!gfx_collision_rect_solid(&demo.collision, pickup_rect));
  }
}

int main(void) {
  test_dimensions_and_camera();
  test_actor_bounds();
  test_pickup_particles();
  test_enemy_restart_clears_pickups();
  test_no_double_move();
  test_slow_terrain_is_walkable();
  test_grey_blocks_remain_solid();
  test_pickups_are_on_walkable_tiles();

  if (failures != 0) {
    fprintf(stderr, "%d game-demo test(s) failed\n", failures);
    return 1;
  }

  puts("game demo tests passed");
  return 0;
}
