#include "mr_autodemo.h"

static unsigned long mr_autodemo_seed_frame;

void mr_autodemo_reset(void) { mr_autodemo_seed_frame = 0UL; }

static void mr_auto_set_move(mr_demo_input_t *out, int dx, int dy) {
  out->dx = dx;
  out->dy = dy;

  if (dx < 0)
    out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_LEFT);
  if (dx > 0)
    out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_RIGHT);
  if (dy < 0)
    out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_UP);
  if (dy > 0)
    out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_DOWN);
}

void mr_autodemo_input(unsigned long frame, mr_demo_input_t *out) {
  unsigned long f;
  unsigned long phase;

  if (!out)
    return;

  out->dx = 0;
  out->dy = 0;
  out->buttons = 0u;

  f = frame + mr_autodemo_seed_frame;

  /* Hold title briefly, then auto-start. */
  if (f >= 24UL && f < 48UL) {
    out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_START);
    return;
  }

  if (f < 48UL) {
    return;
  }

  /* 70 Hz shaped script. A 16-second loop walks through all major systems. */
  phase = ((f - 48UL) / 70UL) % 16UL;

  switch ((int)phase) {
  case 0:
  case 1:
    mr_auto_set_move(out, 1, 0);
    break;
  case 2:
  case 3:
    mr_auto_set_move(out, 0, 1);
    break;
  case 4:
  case 5:
    mr_auto_set_move(out, -1, 0);
    break;
  case 6:
  case 7:
    mr_auto_set_move(out, 0, -1);
    break;
  case 8:
    mr_auto_set_move(out, 1, 1);
    break;
  case 9:
    mr_auto_set_move(out, 1, -1);
    break;
  case 10:
    mr_auto_set_move(out, -1, 1);
    break;
  case 11:
    mr_auto_set_move(out, -1, -1);
    break;
  case 12:
    mr_auto_set_move(out, 1, 0);
    if (((f / 12UL) & 1UL) == 0UL) {
      out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_ACTION);
    }
    break;
  case 13:
    mr_auto_set_move(out, 0, 1);
    break;
  case 14:
    mr_auto_set_move(out, -1, 0);
    if (((f / 20UL) & 1UL) == 0UL) {
      out->buttons = (uint16_t)(out->buttons | MR_DEMO_INPUT_ACTION);
    }
    break;
  default:
    mr_auto_set_move(out, 0, -1);
    break;
  }
}
