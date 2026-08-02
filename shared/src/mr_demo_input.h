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

typedef struct mr_demo_input {
  int dx;
  int dy;
  uint16_t buttons;
} mr_demo_input_t;

#endif /* MR_DEMO_INPUT_H */
