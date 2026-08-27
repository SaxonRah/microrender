#ifndef MR_DEMO_INPUT_H
#define MR_DEMO_INPUT_H

#include <stdint.h>

#define MR_DEMO_INPUT_UP 0x0001u
#define MR_DEMO_INPUT_DOWN 0x0002u
#define MR_DEMO_INPUT_LEFT 0x0004u
#define MR_DEMO_INPUT_RIGHT 0x0008u
#define MR_DEMO_INPUT_ACTION 0x0010u
#define MR_DEMO_INPUT_START 0x0020u
#define MR_DEMO_INPUT_PAUSE 0x0040u
#define MR_DEMO_INPUT_DEBUG 0x0080u
#define MR_DEMO_INPUT_QUIT 0x8000u

/* These are one-shot events, unlike the directional bits which describe held
   state. Frontends with a fixed timestep must preserve them until a simulation
   step consumes them, then clear them before any additional catch-up steps. */
#define MR_DEMO_INPUT_EDGE_MASK                                                \
  (MR_DEMO_INPUT_ACTION | MR_DEMO_INPUT_START | MR_DEMO_INPUT_PAUSE |          \
   MR_DEMO_INPUT_DEBUG | MR_DEMO_INPUT_QUIT)

typedef struct mr_demo_input {
  int dx;
  int dy;
  uint16_t buttons;
} mr_demo_input_t;

#endif /* MR_DEMO_INPUT_H */
