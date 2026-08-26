#include "mr_timestep.h"

#define MR_TIMESTEP_MIN_HZ 1
#define MR_TIMESTEP_MAX_HZ 1000
#define MR_TIMESTEP_MAX_CAP 64

/* One second. Longer than any real frame on any target here, and short enough
   that a broken clock is caught on the first bad read. */
#define MR_TIMESTEP_GLITCH_US 1000000ul

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

  /* An accumulator that was never initialised is all zeroes, and zeroes are a
     silent trap: step_us of 0 and max_steps of 0 make this return 0 forever,
     so the renderer keeps drawing at full speed while the simulation never
     advances. On hardware that presents as a frozen scene at a healthy frame
     rate, which reads as a hang in the renderer rather than a missing call.
     Defaulting is better than sitting still: a wrong-but-moving rate is
     obvious, an unmoving one is not. */
  if (ts->step_us == 0ul || ts->max_steps <= 0)
    mr_timestep_init(ts, 60, 5);

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

  /* A delta beyond any plausible frame is not a slow frame, it is a broken
     clock: a counter that ran backwards, a timer read torn across an update,
     a wrap mishandled upstream. Unsigned subtraction turns any backwards step
     into a huge positive number, so clamping such a delta to the cap would
     silently run max_steps every frame and the simulation would sprint.
     That is exactly what a mis-read PIT did on DOS -- an 11x speedup rather
     than a visible hiccup. Drop these instead of clamping them; one skipped
     frame of simulation is invisible, a permanent speedup is not. */
  if (delta > MR_TIMESTEP_GLITCH_US)
    return 0;

  /* Below that, a genuinely slow frame is clamped rather than banked. Banking
     it means a breakpoint or a USB enumeration pause is paid back max_steps at
     a time over the following frames, so the game runs fast after every
     hitch. */
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
