#include "mr_timestep.h"

#define MR_TIMESTEP_MIN_HZ 1
#define MR_TIMESTEP_MAX_HZ 1000
#define MR_TIMESTEP_MAX_CAP 64

void mr_timestep_init(mr_timestep_t *ts, int hz, int max_steps) {
  if (!ts)
    return;

  if (hz < MR_TIMESTEP_MIN_HZ)
    hz = MR_TIMESTEP_MIN_HZ;
  if (hz > MR_TIMESTEP_MAX_HZ)
    hz = MR_TIMESTEP_MAX_HZ;

  if (max_steps < 1)
    max_steps = 1;
  if (max_steps > MR_TIMESTEP_MAX_CAP)
    max_steps = MR_TIMESTEP_MAX_CAP;

  ts->step_us = 1000000ul / (unsigned long)hz;
  if (ts->step_us == 0ul)
    ts->step_us = 1ul;
  ts->accum_us = 0ul;
  ts->last_us = 0ul;
  ts->max_steps = max_steps;
  ts->started = 0;
}

void mr_timestep_reset(mr_timestep_t *ts) {
  if (!ts)
    return;
  ts->accum_us = 0ul;
  ts->last_us = 0ul;
  ts->started = 0;
}

int mr_timestep_advance(mr_timestep_t *ts, unsigned long now_us) {
  unsigned long delta;
  unsigned long budget;
  int steps;

  if (!ts)
    return 0;

  /* The first call has no previous timestamp to subtract, so it establishes
     one and runs nothing. Treating an unknown delta as zero is right: the
     alternative is interpreting a boot-time counter as elapsed game time. */
  if (!ts->started) {
    ts->last_us = now_us;
    ts->started = 1;
    return 0;
  }

  /* Unsigned subtraction is correct across a counter wrap, which DOS does
     about every 71 minutes. */
  delta = now_us - ts->last_us;
  ts->last_us = now_us;

  /* Discard anything beyond what the cap could consume anyway. Without this
     the accumulator banks the whole stall -- a breakpoint, a long flash write,
     a USB enumeration pause -- and then pays it back max_steps at a time over
     many frames, so the game runs fast for a while after every hitch. */
  budget = (unsigned long)ts->max_steps * ts->step_us;
  if (delta > budget)
    delta = budget;

  ts->accum_us += delta;

  steps = 0;
  while (ts->accum_us >= ts->step_us && steps < ts->max_steps) {
    ts->accum_us -= ts->step_us;
    ++steps;
  }

  return steps;
}
