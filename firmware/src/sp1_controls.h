/*
 * LYRA-4 — faders and the two button ladders.
 *
 * ⚠️ THE LADDER THRESHOLDS CANNOT BE WRITTEN FROM A DESK. They are per-device
 * analog measurements, and the published tables from sp1-midi are only valid under
 * ITS ADC configuration (gain 1/4 + VDD/4), not ours (gain 1/6 + 0.6 V internal).
 * Mixing the two silently produces a table that looks right and misbehaves.
 *
 * It shipped in two stages, the same way the battery divider did, and BOTH ARE NOW
 * DONE:
 *   M1d-a  guided capture (sp1_calib.c) — the firmware names each target and records
 *          the answer against that name, so the table cannot be mis-assembled.
 *   M1d-b  decode. ✅ Tables measured 2026-09-19 from logs/sp1-20260919-182353.log.
 *          See the long comment above the tables in sp1_controls.c for the model
 *          that fills in the combinations, and for why only SINGLE presses are
 *          dispatched.
 *
 * ⚠️ P1.10 (BTN_COM) POWERS BOTH LADDERS AND ALL FOUR FADERS. Until it is driven
 * high, all six channels read ~0 — and that is not a quiet failure, it looks like
 * three unrelated hardware bugs at once (dead faders, dead capture, flickering LEDs
 * from noise on the floating pins). It cost the first M1d-a build. sp1_controls_init()
 * raises it and sp1_power_off() drops it on the way to sleep, so the ON loop calls
 * sp1_controls_rail_on() on entry rather than trusting the last transition.
 *
 * ---- the rail-sag problem ----
 * T1-T4 and Play sit on a resistor ladder fed by a common rail (P1.10) that ALSO
 * feeds the four faders. Pressing any ladder button sags that rail and every fader
 * reading sags with it. So each scan:
 *      1. reads both ladders            (before)
 *      2. reads the four faders
 *      3. reads both ladders again      (after)
 * and marks the fader sample invalid if the rail was loaded at either end. A
 * sustained hold would freeze the faders forever, which is exactly why "••" is the
 * shift and not a track button -- "••" is a direct GPIO, off this rail entirely.
 */
#ifndef SP1_CONTROLS_H
#define SP1_CONTROLS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* size_t, for sp1_controls_raw_line -- this header must
                       * stand alone, not rely on <zephyr/kernel.h> being
                       * included first. sp1_calib.c includes it before kernel.h
                       * and the build fails without this. */

#define SP1_NUM_FADERS 4

/* The nine ladder buttons, in the order the hardware presents them. */
enum sp1_button {
	SP1_BTN_PLAY = 0,
	SP1_BTN_T1, SP1_BTN_T2, SP1_BTN_T3, SP1_BTN_T4,
	SP1_BTN_VOL_UP, SP1_BTN_VOL_DOWN,
	SP1_BTN_FFWD, SP1_BTN_RWD,
	SP1_BTN_COUNT
};

/* Rail load, from the instantaneous ladder reads. */
enum sp1_rail {
	SP1_RAIL_IDLE = 0,   /* nothing pressed; fader reads are trustworthy   */
	SP1_RAIL_LOADED,     /* something pressed; fader reads are sagged      */
};

int  sp1_controls_init(void);

/* Drive BTN_COM (P1.10) high: the rail that powers both ladders and all four faders.
 * Idempotent and cheap. sp1_controls_init() calls it; call it again on every entry to
 * ON, because sp1_power_off() drops it. Without it every control reads zero. */
void sp1_controls_rail_on(void);

/* One scan. Call once per control tick. Reads ladders, faders, ladders again. */
void sp1_controls_scan(void);

/* Fader position 0..4095, or the last VALID value if the rail was loaded during
 * the most recent scan. Never returns a sagged reading. */
uint16_t sp1_fader_raw(int i);

/* True if the most recent scan produced a trustworthy fader sample. From M3d that
 * includes a scan taken while FFWD or RWD alone was held, with the rail sag corrected
 * (see sp1_controls_scan); every other press is still rejected. */
bool sp1_faders_valid(void);

/* The last valid sample was sag-CORRECTED (FFWD/RWD held), not clean. Diagnostic. */
bool sp1_faders_compensated(void);

/* How hard the speaker amplifier is working, 0..1 (main feeds it the play-row meter's
 * level every tick). Scales SP1_SAG_AUDIO_K in sp1_controls.c, which is 0 until the
 * amplifier's share of the rail droop has been measured on hardware -- so this is a
 * no-op today and exists so the measurement has somewhere to land (M4a). */
void sp1_controls_set_audio_load(float load);

/* THIS scan's fader sample, sag included. For the sag measurement ONLY -- the UI
 * must use sp1_fader_raw(), which never returns a sagged reading. */
uint16_t sp1_fader_scratch(int i);

/* Fader position scaled to 0-255, for driving its own track LED's brightness. */
uint8_t sp1_fader_level(int i);

/* Debounced button state.
 *
 * ⚠️ SINGLE PRESSES ONLY. A recognised chord reports as "nothing pressed", on
 * purpose: in the crowded top of ladder 0 the states sit 26-55 counts apart, so a
 * chord resolved to its nearest single would fire a real UI action from a fumbled
 * press. The 31-state table exists so chords can be recognised and DISCARDED. Use
 * sp1_chord_mask() if you ever want them. */
bool sp1_button_held(enum sp1_button b);

/* True on the tick a button went down / came up. */
bool sp1_button_pressed(enum sp1_button b);
bool sp1_button_released(enum sp1_button b);

enum sp1_rail sp1_rail_state(void);

/* ---- "was any other control touched?" — the shift-suppression input ----
 *
 * Call sp1_controls_activity_mark() on the tick "••" goes down, then
 * sp1_controls_activity() each tick while it is held; a true result means "••" is
 * being used as a SHIFT and shutdown must be suppressed (sp1_shift_used()).
 * Clear on release.
 *
 * Works WITHOUT the threshold tables: it reports that *something* was touched, not
 * which. Any ladder button down shows up as a loaded rail, and fader movement is
 * measured against the mark. That is what lets shift suppression ship in M1d-a
 * rather than waiting for M1d-b. */
void sp1_controls_activity_mark(void);
void sp1_controls_activity_clear(void);
bool sp1_controls_activity(void);

/* Describe what made the last sp1_controls_activity() return true -- "button T2
 * held", "chord 0x03", or "fader F3 moved 1502 -> 1741 since "••" went down". For
 * the suppression log line: ladder values alone can rule a button out but cannot
 * say what it WAS. */
int sp1_controls_activity_describe(char *buf, size_t len);

/* Raw channel values, for the M1d-a capture and for diagnosing a bad table. */
uint16_t sp1_ladder_raw(int which);        /* 0 = tracks+play, 1 = vol+rocker */

/* Format one capture line into buf. Returns the length written. */
int sp1_controls_raw_line(char *buf, size_t len);

/* Raw ladder-0 state including chords, as a bitmask: T1 0x01, T2 0x02, T3 0x04,
 * T4 0x08, PLAY 0x10. Zero means nothing matched a known state. Diagnostics, and
 * any future UI that wants chords. */
uint8_t sp1_chord_mask(void);

/* Drop all button state and adopt whatever is currently held WITHOUT emitting
 * press/release edges. Call on entry to ON so a finger still resting on a button
 * across the transition does not read as a fresh press. */
void sp1_controls_reset_buttons(void);

/* True once the threshold tables have been populated with measured values.
 * While false, button decoding is disabled rather than guessed at. */
bool sp1_controls_calibrated(void);

#endif /* SP1_CONTROLS_H */
