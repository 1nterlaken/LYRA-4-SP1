/*
 * LYRA-4 — the play row (side row) while ON: two layers, composited (M2b).
 *
 * ---- the spec (Adara, M2b) ----
 *   - FOREGROUND: the dB meter while Plaits is selected; the clock while Marbles is.
 *   - BACKGROUND: the other one, capped at SP1_ROW_BG_MAX (~10 %).
 *   - The clock is the old M0 heartbeat -- one LED stepping from the "••" end toward
 *     PLAY -- with the cross-fade REMOVED: a hard step, like a clock. Since M4 it is
 *     Marbles' clock: one step per t2 tick, standing still while stopped.
 *   - The point of the background: the row is never fully dark while the device is
 *     ON, so ON is visible even with every fader at zero and no sound.
 *
 * ---- compositing ----
 * Per LED, the brighter of the two layers wins: out = max(fg, bg). The background is
 * capped at SP1_ROW_BG_MAX, so it only ever shows where the foreground is darker than
 * that -- it can never make a meter reading look louder than it is, except on an LED
 * the meter has left completely dark (or below 10 %).
 *
 * Above 80 % the rule flips (M3): the background DIMS the foreground by up to its own
 * level, fading in from 80 % to 100 %, so the clock stays visible as a notch in a full
 * bar. See composite() in sp1_playrow.c.
 *
 * ---- cost ----
 * Four compares and four sp1_led_set() calls per control tick. The layers are
 * computed whether or not they are in front, so switching foreground is instant and
 * the clock keeps its phase.
 *
 * Nothing here runs in the audio path.
 */
#ifndef SP1_PLAYROW_H
#define SP1_PLAYROW_H

#include <stdint.h>

enum sp1_playrow_fg {
	SP1_FG_METER = 0,   /* Plaits selected (the only engine layer until M6) */
	SP1_FG_CLOCK,       /* Marbles selected                                 */
};

/* Blank both layers, restart the clock at the "••" end, meter forgets its window.
 * Call on every entry to ON. Foreground selection is kept. */
void sp1_playrow_reset(void);

/* Choose which layer is in front. */
void sp1_playrow_set_fg(enum sp1_playrow_fg fg);

/* The clock layer steps once per Marbles t2 tick (M4): main calls this with the
 * number of ticks since the last call. Stopped, the clock simply stands still. */
void sp1_playrow_clock_step(uint32_t ticks);
/* Back to the "••" end (PLAY starts the clock on a beat). */
void sp1_playrow_clock_reset(void);

/* One control tick: update the meter from the audio thread's peak, composite, draw. */
void sp1_playrow_tick(uint32_t elapsed_ms);

#endif /* SP1_PLAYROW_H */
