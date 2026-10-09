/*
 * LYRA-4 — battery voltage and charge estimate.
 *
 * ⚠️ THE DIVIDER RATIO IS PROVISIONAL. The SP-1-dev wiki says only that the cell
 * is "directly tied to PIN_BATT_LEVEL over a voltage divider" and does not give
 * the ratio. SP1_BATT_DIVIDER_MILLI below is an assumption, so every percentage
 * this module reports carries an unknown scale error until it is calibrated.
 *
 * Calibrating it needs no multimeter and no disassembly. The BQ24232 terminates
 * at 4.20 V and says so: nCHG goes high while nPGOOD stays low. At that moment
 * the cell is at 4.20 V by definition, so
 *
 *     divider = 4.20 / (raw / 4095 * 3.6)
 *
 * The console (M1c) prints that line for you: plug in with a full battery and read
 * the CALIBRATION line. sp1_batt_raw() is what it reports.
 */
#ifndef SP1_BATT_H
#define SP1_BATT_H

#include <stdint.h>
#include <stdbool.h>

/* Cell-to-AIN4 divider, x1000 (2000 == a /2 divider).
 *
 * MEASURED 2026-09-20 from 28 console samples with the charger in CV taper, where
 * the BQ24232 holds the terminal at its 4.20 V target:
 *
 *     mean raw 2382.9  ->  V_pin 2094.9 mV  ->  divider 2.0049
 *     per-sample spread 2375..2392 (17 counts) = +/-15 mV at the cell
 *
 * So the /2 guess was right to within the measurement noise -- the correction is
 * 10.5 mV against +/-15 mV of scatter. 2005 is kept because it makes a full cell
 * read as full rather than ~98 %, but do NOT trust the third digit. A reading taken
 * at true termination (chg=0) would confirm it; the difference will be invisible. */
#define SP1_BATT_DIVIDER_MILLI  2005u

/* Bring up the SAADC channel for AIN4. Returns 0 on success; on failure the
 * readers below report a safe midpoint rather than faulting. */
int sp1_batt_init(void);

/* Sample the battery. Call at most every few hundred ms -- it blocks for the
 * conversion. */
void sp1_batt_sample(void);

/* Last raw 12-bit SAADC reading. For calibration. */
uint16_t sp1_batt_raw(void);

/* Last reading in millivolts at the CELL, i.e. after the divider is applied.
 * Carries the provisional-divider error. */
uint16_t sp1_batt_mv(void);

/* Estimated charge, 0-255 for direct use as a bar level.
 *
 * ⚠️ Reads HIGH while charging: the charger holds the terminal voltage above the
 * cell's resting voltage, so a charging battery always looks fuller than it is.
 * nCHG going high is the trustworthy "complete" signal, not this number. */
uint8_t sp1_batt_level(void);

/* True once the reading is meaningful (at least one sample taken). */
bool sp1_batt_valid(void);

#endif /* SP1_BATT_H */
