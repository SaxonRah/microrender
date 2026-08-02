#include "gfx_engine.h"

static int ge_max_int(int a, int b) { return a > b ? a : b; }
static int ge_min_int(int a, int b) { return a < b ? a : b; }

static int ge_floor_div(int a, int b) {
  int q, r;
  if (b <= 0)
    return 0;
  q = a / b;
  r = a % b;
  if (r < 0)
    --q;
  return q;
}

void gfx_actor_init(gfx_actor_t GFX_PTR *a, const gfx_sprite_t GFX_PTR *sprite,
                    int world_x, int world_y, int z, int visible) {
  if (!a)
    return;
  gfx_sprite_instance_init(&a->inst, sprite, world_x, world_y, z, visible);
  a->anim = 0;
  a->anim_index = 0;
  a->anim_tick = 0;
  a->anim_dir = 1;
  a->world_x = world_x;
  a->world_y = world_y;
  a->vx = 0;
  a->vy = 0;
  a->min_x = -32767;
  a->min_y = -32767;
  a->max_x = 32767;
  a->max_y = 32767;
  a->collider_x = 0;
  a->collider_y = 0;
  a->collider_w = sprite ? sprite->width : 0;
  a->collider_h = sprite ? sprite->height : 0;
  a->flags = (uint8_t)((visible ? GFX_ACTOR_VISIBLE : 0u) | GFX_ACTOR_ACTIVE);
  a->state = 0;
  a->blocked_x = 0;
  a->blocked_y = 0;
}

void gfx_actor_set_bounds(gfx_actor_t GFX_PTR *a, int min_x, int min_y,
                          int max_x, int max_y) {
  if (!a)
    return;
  a->min_x = min_x;
  a->min_y = min_y;
  a->max_x = max_x;
  a->max_y = max_y;
}

void gfx_actor_set_anim(gfx_actor_t GFX_PTR *a,
                        const gfx_anim_t GFX_PTR *anim) {
  if (!a)
    return;
  a->anim = anim;
  a->anim_index = 0;
  a->anim_tick = 0;
  a->anim_dir = 1;
  if (anim && anim->frame_count > 0)
    a->inst.sprite = anim->frames[0].sprite;
}

void gfx_actor_set_velocity(gfx_actor_t GFX_PTR *a, int vx, int vy) {
  if (!a)
    return;
  a->vx = vx;
  a->vy = vy;
}

void gfx_actor_set_state(gfx_actor_t GFX_PTR *a, uint8_t state) {
  if (a)
    a->state = state;
}

void gfx_actor_set_collider(gfx_actor_t GFX_PTR *a, int x, int y, int w,
                            int h) {
  if (!a)
    return;
  a->collider_x = x;
  a->collider_y = y;
  a->collider_w = w;
  a->collider_h = h;
}

gfx_rect_t gfx_actor_world_rect(const gfx_actor_t GFX_PTR *a) {
  if (!a)
    return gfx_rect_make(0, 0, 0, 0);
  return gfx_rect_make(a->world_x + a->collider_x, a->world_y + a->collider_y,
                       a->collider_w, a->collider_h);
}

void gfx_collision_init(gfx_collision_map_t GFX_PTR *cm, int map_w, int map_h,
                        int tile_w, int tile_h, const uint8_t GFX_PTR *flags,
                        uint8_t solid_mask) {
  if (!cm)
    return;
  cm->map_w = map_w;
  cm->map_h = map_h;
  cm->tile_w = tile_w;
  cm->tile_h = tile_h;
  cm->flags = flags;
  cm->solid_mask = solid_mask;
}

int gfx_collision_tile_solid(const gfx_collision_map_t GFX_PTR *cm, int tx,
                             int ty) {
  if (!cm || !cm->flags || tx < 0 || ty < 0 || tx >= cm->map_w ||
      ty >= cm->map_h)
    return 0;
  return (cm->flags[ty * cm->map_w + tx] & cm->solid_mask) != 0u;
}

int gfx_collision_rect_solid(const gfx_collision_map_t GFX_PTR *cm,
                             gfx_rect_t world_rect) {
  int tx0, ty0, tx1, ty1, tx, ty;
  if (!cm || !cm->flags || cm->tile_w <= 0 || cm->tile_h <= 0)
    return 0;
  if (world_rect.w <= 0 || world_rect.h <= 0)
    return 0;
  tx0 = ge_floor_div(world_rect.x, cm->tile_w);
  ty0 = ge_floor_div(world_rect.y, cm->tile_h);
  tx1 = ge_floor_div(world_rect.x + world_rect.w - 1, cm->tile_w);
  ty1 = ge_floor_div(world_rect.y + world_rect.h - 1, cm->tile_h);
  if (tx1 < 0 || ty1 < 0 || tx0 >= cm->map_w || ty0 >= cm->map_h)
    return 0;
  if (tx0 < 0)
    tx0 = 0;
  if (ty0 < 0)
    ty0 = 0;
  if (tx1 >= cm->map_w)
    tx1 = cm->map_w - 1;
  if (ty1 >= cm->map_h)
    ty1 = cm->map_h - 1;
  for (ty = ty0; ty <= ty1; ++ty)
    for (tx = tx0; tx <= tx1; ++tx)
      if (gfx_collision_tile_solid(cm, tx, ty))
        return 1;
  return 0;
}

static int actor_rect_inside_bounds(const gfx_actor_t GFX_PTR *a) {
  gfx_rect_t r = gfx_actor_world_rect(a);
  return r.x >= a->min_x && r.y >= a->min_y && r.x + r.w <= a->max_x &&
         r.y + r.h <= a->max_y;
}

static int actor_position_blocked(const gfx_actor_t GFX_PTR *a,
                                  const gfx_collision_map_t GFX_PTR *cm) {
  return !actor_rect_inside_bounds(a) ||
         (cm && gfx_collision_rect_solid(cm, gfx_actor_world_rect(a)));
}

static int
distance_to_next_vertical_tile_edge(const gfx_actor_t GFX_PTR *a,
                                    const gfx_collision_map_t GFX_PTR *cm,
                                    int dx) {
  gfx_rect_t r = gfx_actor_world_rect(a);
  int edge, tile_edge, d;
  if (!cm || cm->tile_w <= 0)
    return dx;
  if (dx > 0) {
    edge = r.x + r.w;
    /* ge_floor_div, not `/`: truncating division rounds toward zero and
       picks the wrong tile edge for actors at negative world coordinates.
       The dx < 0 branch below already floors; these must agree. */
    tile_edge = (ge_floor_div(edge, cm->tile_w) + 1) * cm->tile_w;
    d = tile_edge - edge;
    if (d < 1)
      d = 1;
    if (d > dx)
      d = dx;
    return d;
  }
  edge = r.x;
  tile_edge = ge_floor_div(edge - 1, cm->tile_w) * cm->tile_w;
  d = edge - tile_edge;
  if (d < 1)
    d = 1;
  if (-d < dx)
    d = -dx;
  return -d;
}

static int
distance_to_next_horizontal_tile_edge(const gfx_actor_t GFX_PTR *a,
                                      const gfx_collision_map_t GFX_PTR *cm,
                                      int dy) {
  gfx_rect_t r = gfx_actor_world_rect(a);
  int edge, tile_edge, d;
  if (!cm || cm->tile_h <= 0)
    return dy;
  if (dy > 0) {
    edge = r.y + r.h;
    /* See the note in distance_to_next_vertical_tile_edge. */
    tile_edge = (ge_floor_div(edge, cm->tile_h) + 1) * cm->tile_h;
    d = tile_edge - edge;
    if (d < 1)
      d = 1;
    if (d > dy)
      d = dy;
    return d;
  }
  edge = r.y;
  tile_edge = ge_floor_div(edge - 1, cm->tile_h) * cm->tile_h;
  d = edge - tile_edge;
  if (d < 1)
    d = 1;
  if (-d < dy)
    d = -dy;
  return -d;
}

void gfx_actor_move_collide(gfx_actor_t GFX_PTR *a,
                            const gfx_collision_map_t GFX_PTR *cm, int dx,
                            int dy) {
  if (!a)
    return;
  a->blocked_x = 0;
  a->blocked_y = 0;
  a->inst.old_x = a->inst.x;
  a->inst.old_y = a->inst.y;

  while (dx != 0) {
    int step = distance_to_next_vertical_tile_edge(a, cm, dx);
    int old_x = a->world_x;
    if (step == 0)
      step = dx > 0 ? 1 : -1;
    a->world_x += step;
    if (actor_position_blocked(a, cm)) {
      a->world_x = old_x;
      a->blocked_x = 1;
      break;
    }
    dx -= step;
  }

  while (dy != 0) {
    int step = distance_to_next_horizontal_tile_edge(a, cm, dy);
    int old_y = a->world_y;
    if (step == 0)
      step = dy > 0 ? 1 : -1;
    a->world_y += step;
    if (actor_position_blocked(a, cm)) {
      a->world_y = old_y;
      a->blocked_y = 1;
      break;
    }
    dy -= step;
  }

  a->inst.x = a->world_x;
  a->inst.y = a->world_y;
  a->inst.visible = (a->flags & GFX_ACTOR_VISIBLE) != 0u;
}

void gfx_camera_init(gfx_camera_t GFX_PTR *cam, int screen_w, int screen_h,
                     int map_w, int map_h) {
  if (!cam)
    return;
  cam->x = 0;
  cam->y = 0;
  cam->screen_w = screen_w;
  cam->screen_h = screen_h;
  cam->map_w = map_w;
  cam->map_h = map_h;
  cam->dead_x = screen_w / 3;
  cam->dead_y = screen_h / 3;
  cam->dead_w = screen_w / 3;
  cam->dead_h = screen_h / 3;
  cam->shake_x = 0;
  cam->shake_y = 0;
  cam->shake_ticks = 0;
}

void gfx_camera_set_deadzone(gfx_camera_t GFX_PTR *cam, int x, int y, int w,
                             int h) {
  if (!cam)
    return;
  cam->dead_x = x;
  cam->dead_y = y;
  cam->dead_w = w;
  cam->dead_h = h;
}

void gfx_camera_follow_rect(gfx_camera_t GFX_PTR *cam, gfx_rect_t target) {
  int left, right, top, bottom, max_x, max_y;
  if (!cam)
    return;
  left = cam->x + cam->dead_x;
  right = left + cam->dead_w;
  top = cam->y + cam->dead_y;
  bottom = top + cam->dead_h;
  if (target.x < left)
    cam->x -= left - target.x;
  if (target.x + target.w > right)
    cam->x += (target.x + target.w) - right;
  if (target.y < top)
    cam->y -= top - target.y;
  if (target.y + target.h > bottom)
    cam->y += (target.y + target.h) - bottom;
  max_x = cam->map_w - cam->screen_w;
  max_y = cam->map_h - cam->screen_h;
  if (max_x < 0)
    max_x = 0;
  if (max_y < 0)
    max_y = 0;
  cam->x = ge_max_int(0, ge_min_int(cam->x, max_x));
  cam->y = ge_max_int(0, ge_min_int(cam->y, max_y));
}

void gfx_camera_apply_shake(gfx_camera_t GFX_PTR *cam, int strength_x,
                            int strength_y, int ticks) {
  if (!cam)
    return;
  cam->shake_x = strength_x;
  cam->shake_y = strength_y;
  cam->shake_ticks = ticks;
}

void gfx_camera_update_shake(gfx_camera_t GFX_PTR *cam) {
  if (!cam)
    return;
  if (cam->shake_ticks > 0)
    --cam->shake_ticks;
  else {
    cam->shake_x = 0;
    cam->shake_y = 0;
  }
}

void gfx_actor_update_animation(gfx_actor_t GFX_PTR *a) {
  const gfx_anim_frame_t GFX_PTR *frame;
  if (!a || !a->anim || a->anim->frame_count <= 0)
    return;
  if (a->anim_index < 0 || a->anim_index >= a->anim->frame_count)
    a->anim_index = 0;
  frame = &a->anim->frames[a->anim_index];
  a->inst.sprite = frame->sprite;
  ++a->anim_tick;
  if (frame->ticks == 0 || a->anim_tick < frame->ticks)
    return;
  a->anim_tick = 0;
  if (a->anim->pingpong) {
    a->anim_index += a->anim_dir;
    if (a->anim_index >= a->anim->frame_count) {
      a->anim_index = a->anim->frame_count > 1 ? a->anim->frame_count - 2 : 0;
      a->anim_dir = -1;
    } else if (a->anim_index < 0) {
      a->anim_index = a->anim->frame_count > 1 ? 1 : 0;
      a->anim_dir = 1;
    }
  } else {
    ++a->anim_index;
    if (a->anim_index >= a->anim->frame_count)
      a->anim_index = a->anim->loop ? 0 : a->anim->frame_count - 1;
  }
  a->inst.sprite = a->anim->frames[a->anim_index].sprite;
}

void gfx_actor_update(gfx_actor_t GFX_PTR *a) {
  if (!a)
    return;
  gfx_actor_update_animation(a);
  a->inst.old_x = a->inst.x;
  a->inst.old_y = a->inst.y;
  a->world_x += a->vx;
  a->world_y += a->vy;
  a->inst.x = a->world_x;
  a->inst.y = a->world_y;
  a->inst.visible = (a->flags & GFX_ACTOR_VISIBLE) != 0u;
}

void gfx_actor_sync_screen(gfx_actor_t GFX_PTR *a, int camera_x, int camera_y) {
  if (!a)
    return;
  a->inst.old_x = a->inst.x;
  a->inst.old_y = a->inst.y;
  a->inst.x = a->world_x - camera_x;
  a->inst.y = a->world_y - camera_y;
  a->inst.visible = (a->flags & GFX_ACTOR_VISIBLE) != 0u;
}

void gfx_actors_mark_dirty(gfx_dirty_list_t GFX_PTR *d,
                           const gfx_actor_t GFX_PTR *actors, int count,
                           int old_camera_x, int old_camera_y, int new_camera_x,
                           int new_camera_y, int pad) {
  int i;
  if (!d || !actors)
    return;
  for (i = 0; i < count; ++i) {
    const gfx_actor_t GFX_PTR *a = &actors[i];
    if (!a->inst.sprite)
      continue;
    gfx_dirty_add_sprite_move(
        d, a->world_x - old_camera_x, a->world_y - old_camera_y,
        a->world_x - new_camera_x, a->world_y - new_camera_y,
        a->inst.sprite->width, a->inst.sprite->height, pad);
  }
}

void gfx_actors_copy_instances(const gfx_actor_t GFX_PTR *actors,
                               gfx_sprite_instance_t GFX_PTR *out_instances,
                               int count) {
  int i;
  if (!actors || !out_instances)
    return;
  for (i = 0; i < count; ++i)
    out_instances[i] = actors[i].inst;
}
