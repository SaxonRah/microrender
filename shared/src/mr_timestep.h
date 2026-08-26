#ifndef MR_TIMESTEP_H
#define MR_TIMESTEP_H

/*
 * Fixed-timestep accumulator.
 *
 * Both demos advance their simulation once per rendered frame, which ties game
 * speed to how fast the hardware draws. That spread is not small: the same
 * game demo runs near 15 FPS on a real 386, 60 on a Pico 2, 140 under DOSBox
 * at high cycles, and several thousand on an uncapped Raylib window. The game
 * is literally a hundred times faster on one target than another, and a faster
 * board makes it worse rather than better.
 *
 * The fix is not to scale movement by delta time -- that makes every result
 * depend on timing jitter and destroys the byte-for-byte reproducibility the
 * fuzz and game tests rely on. Instead, keep the simulation step fixed and
 * exactly as it is, and let wall-clock time decide how many steps to run:
 *
 *     n = mr_timestep_advance(&ts, now_us);
 *     while (n-- > 0)
 *       mr_game_demo_tick(&demo, &input);
 *
 * Every step is still identical and deterministic. Only the count varies. Tests
 * that want reproducibility call tick() directly and never involve this at all.
 *
 * At high frame rates most frames run zero steps, re-presenting the same state.
 * That is correct and is the point: rendering and simulation are separate
 * rates. At low frame rates several steps run per frame, capped so that a
 * stall cannot demand more simulation than the next frame can afford -- which
 * would make the following frame slower still, and the one after that worse.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unsigned long step_us;  /* microseconds per simulation step */
  unsigned long accum_us; /* unspent time carried to the next frame */
  unsigned long last_us;  /* previous timestamp, for delta */
  int max_steps;          /* per-advance ceiling */
  int started;            /* first advance establishes last_us only */
} mr_timestep_t;

/* hz is the simulation rate (60 is typical). max_steps caps how many steps a
   single advance may return; 5 is a reasonable default. Both are clamped to
   sane values, so a zero or negative argument cannot produce a divide by zero
   or an unbounded loop. */
void mr_timestep_init(mr_timestep_t *ts, int hz, int max_steps);

/* Number of fixed steps to run now. now_us may wrap (DOS wraps roughly every
   71 minutes); unsigned subtraction handles that correctly. A delta larger
   than max_steps worth of time is discarded rather than banked, so pausing in
   a debugger does not cause a burst of catch-up simulation. */
int mr_timestep_advance(mr_timestep_t *ts, unsigned long now_us);

/* Forget the previous timestamp. Call after a pause, mode switch, or anything
   else that leaves a large meaningless gap. */
void mr_timestep_reset(mr_timestep_t *ts);

#ifdef __cplusplus
}
#endif

#endif /* MR_TIMESTEP_H */
