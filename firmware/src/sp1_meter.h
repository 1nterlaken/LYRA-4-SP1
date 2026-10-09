/*
 * LYRA-4 — the dB meter on the play row (M2, reworked M2b).
 *
 * The meter only COMPUTES levels. sp1_playrow owns the row: it composites the meter
 * with the clock layer and draws the result.
 *
 * ---- the spec (Adara, M2) ----
 *   - fills from the "••" end toward PLAY
 *   - -inf is blank
 *   - four equal gain bands, one per LED; each LED's BRIGHTNESS shows where the level
 *     sits inside its band
 *   - the LED next to PLAY covers 0 to -6 dB, and the others follow in equal steps
 *
 * ---- the bands: 6.02 dB, i.e. factors of TWO ----
 *
 *      LED (from "••")      0              1              2              3 (next to PLAY)
 *      band           -24 .. -18 dB  -18 .. -12 dB  -12 .. -6 dB    -6 .. 0 dBFS
 *
 * Each band is one halving of amplitude. That is the whole trick behind the CPU cost:
 *
 * ---- why this costs almost nothing ----
 * A dB meter normally needs a log10 per update -- tens of cycles in software on an M4
 * with no hardware log. Here there is NO LOG AND NO FLOATING POINT AT ALL. Because
 * every band boundary is a power of two, "which band is this peak in" is simply the
 * position of its highest set bit, which the Cortex-M4 answers in ONE instruction
 * (CLZ, count leading zeros). The position INSIDE the band comes from the next seven
 * bits through a 128-byte table of log2 values, so brightness is linear in dB rather
 * than in amplitude -- which is what makes equal dB read as equal brightness steps.
 *
 * The bands are 6.02 dB, not 6.00: that is what makes them exact powers of two. The
 * 0.02 dB difference is invisible and buys the single-instruction band lookup.
 *
 * ---- the three costs, and where they are paid ----
 *   audio path  : |x| and a max on every 4th sample (M2b), while the block is
 *                 generated. <1 cycle per sample averaged, ~0.06 % of the audio
 *                 budget. NOTHING ELSE touches audio.
 *   hand-off    : one atomic max per 5.33 ms block.
 *   control loop: CLZ + shift + one table read + a 3-entry max, ~25 cycles, ~120 Hz --
 *                 about 0.004 % of the CPU, and off the audio deadline entirely.
 *
 * ---- no ballistics: a 3-tick peak window, nothing else (M2b) ----
 * M2 had a 12 dB/s release. On hardware it read as a decay animation left over from
 * the old heartbeat, not as audio, so it is gone. The display is the peak of the last
 * THREE control ticks (~25 ms) -- a hard window, not a fall-off: silence goes blank
 * within ~25 ms, which is below what the eye resolves as motion.
 *
 * Why three ticks and not one: a single ~8 ms window is shorter than one period of
 * anything under ~120 Hz, so a bass note's peak would land in some windows and not
 * others and the meter would flicker and under-read. 25 ms holds a full period down
 * to 40 Hz. That is accuracy, not smoothing.
 *
 * ---- "signal present" glint ----
 * LED 1 (the "••" end) sits at SP1_METER_GLINT (~15 %) for anything from -48 dBFS up
 * to the bottom of its band, so a quiet tail is distinguishable from true silence.
 * Inside its band it rises from the glint level to full, so it never dips BELOW the
 * glint on the way up -- the meter stays monotone.
 *
 * ⚠️ Peak is sampled on every 4th sample in the audio path (M2b). Against an 8 ms
 * window of a moving signal that catches the true peak to within a fraction of a dB;
 * the worst case is a steady tone at an exact sub-multiple of 12 kHz (3 kHz reads up
 * to 3 dB low). Irrelevant for a meter you read by eye.
 */
#ifndef SP1_METER_H
#define SP1_METER_H

#include <stdint.h>

/* Meter position in 1/64-band units: 0 = -24 dBFS (blank), 256 = 0 dBFS (full).
 * Four bands of 64. Exposed for tests and diagnostics. */
#define SP1_METER_FULL 256u

/* Pure conversion, no state: absolute peak sample (0..32767, int16 full scale)
 * -> meter position 0..255. Exposed so it can be verified in isolation. */
uint16_t sp1_meter_level_from_peak(uint32_t peak);

/* Brightness (0..255, perceptual -- sp1_led applies the gamma) of play-row LED `i`
 * for a meter position, WITHOUT the glint. i = 0 is the "••" end, i = 3 is next to
 * PLAY. Exposed for tests. */
uint8_t sp1_meter_led_level(uint16_t level, int i);

/* Peak at or above this is "signal present": 2^7 = -48.16 dBFS. */
#define SP1_METER_PRESENT 128u

/* Reset to blank and forget the window. Call on entry to ON. */
void sp1_meter_reset(void);

/* One control tick: take the peak accumulated by the audio thread since the last
 * call, push it into the 3-tick window, and compute the four LED levels. Draws
 * NOTHING -- sp1_playrow composites it with the clock and owns the row. */
void sp1_meter_tick(void);

/* The four LED levels (0..255 perceptual) from the last tick, index 0 = "••" end. */
void sp1_meter_leds(uint8_t out[4]);

/* Current displayed position (0..255), for diagnostics. */
uint16_t sp1_meter_level(void);

#endif /* SP1_METER_H */
