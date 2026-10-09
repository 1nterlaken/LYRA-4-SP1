/*
 * LYRA-4 — guided, SELF-LABELLING ladder calibration.
 *
 * ---- why this exists ----
 * The M1d-a raw capture worked: it produced clean, tight plateaus (spreads of ~6
 * counts out of 4095). What it could not produce was *labels*. A 73-second firehose
 * of numbers tells you that eleven distinct levels exist on the tracks ladder; it
 * cannot tell you which one was T2 and which was T1+T4, because that lives in the
 * tester's memory and has to survive being written down in the right order.
 *
 * On the first attempt it did not survive: the capture contains eight presses where
 * the procedure called for nine, several repeats, and a part-5 sag measurement that
 * was performed with every fader parked at zero, where sag is invisible by
 * construction. None of that is a testing failure — it is a procedure that asked a
 * human to be a reliable data logger while also operating the device.
 *
 * So the firmware labels the data itself. It names one target at a time, waits for a
 * stable plateau, records it against that name, and waits for release before moving
 * on. The operator's whole job becomes "press what it just asked for". At the end it
 * prints a paste-ready C table.
 *
 * ---- properties that matter ----
 *   - TICK-DRIVEN AND NON-BLOCKING. Never spins, never waits on the console. The
 *     watchdog keeps being fed by the main loop and "••" keeps working throughout,
 *     which on a device with no reset pin is not negotiable.
 *   - A step that is not answered within SP1_CALIB_STEP_TIMEOUT_MS is marked SKIPPED
 *     and the run moves on. That is the escape hatch: wait it out. There is no
 *     "cancel" chord, because every chord is a control this is trying to measure.
 *   - It prints the live ladder value about once a second while waiting, so a step
 *     that never triggers still leaves evidence of what the ADC was seeing.
 */
#ifndef SP1_CALIB_H
#define SP1_CALIB_H

#include <stdint.h>
#include <stdbool.h>

/* ---- re-running calibration once the tables are populated ----
 * Set this to 1 and rebuild. It is a header constant and NOT a build-system flag
 * because `west build -- -DSP1_FORCE_CALIB=1` does not do what it looks like: after
 * the `--`, -D defines a CMAKE variable, not a C preprocessor macro, so the #if
 * never fires and calibration silently does not run. That cost a flash cycle on
 * 2026-09-19. One line to edit here, no build-system subtleties. */
#define SP1_FORCE_CALIB 0   /* ⚠️ back to 0 after every re-run: at 1 the wizard
                             * restarts on EVERY entry to ON, which is exactly what
                             * happened at the end of the 2026-09-20 session */

/* Begin a run from the top. Safe to call again to restart. */
void sp1_calib_start(void);

/* Advance one control tick. No-op when not running. */
void sp1_calib_tick(uint32_t elapsed_ms);

/* True while a run is in progress. */
bool sp1_calib_running(void);

#endif /* SP1_CALIB_H */
