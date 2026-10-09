/* LYRA-4: "has a hand touched a fader since the mark?" that survives common-mode drift.
 *
 * The stock check (sp1_controls_activity) flags any fader that has moved SP1_TOUCH_COUNTS
 * (80) from the value it had when "••" went down, and that flag cancels the 3 s shutdown.
 * A loud, changing output loads the supply rail the faders are read from, so ALL four
 * faders read a little low (or high) together, in proportion to their value. LYRA holds
 * "••" for a long time while the sound is moving, so that drift can cross 80 counts with
 * no hand on anything.
 *
 * A hand moves one or two faders. Drift moves every fader by the same FACTOR. So: estimate
 * the common factor from the faders that are far enough off zero to measure it (median of
 * their now/mark ratios, needs at least 3), bound it, and judge each fader against
 * mark * factor. With fewer than 3 usable faders, or a factor outside the bound, the factor
 * is 1 and this is exactly the stock test.
 *
 * Pure function, no state, no Zephyr: host-tested in lyra_drifttest.c.
 */
#ifndef LYRA_DRIFT_H
#define LYRA_DRIFT_H

#include <stdint.h>

#define LYRA_DRIFT_TOUCH     80      /* same as SP1_TOUCH_COUNTS                          */
#define LYRA_DRIFT_MIN_MARK  1200    /* a fader below this says too little about gain     */
#define LYRA_DRIFT_G_MIN     0.93f   /* more drift than +-7 % is not drift we trust       */
#define LYRA_DRIFT_G_MAX     1.07f

/* Returns the index (0..3) of the first fader judged touched, or -1 if none.
 * `gain_out` (optional) receives the common factor that was used. */
static inline int lyra_fader_touched(const uint16_t now[4], const uint16_t mark[4], float *gain_out)
{
	float r[4];
	int n = 0;
	for (int i = 0; i < 4; i++) {
		if (mark[i] >= LYRA_DRIFT_MIN_MARK) {
			r[n++] = (float)now[i] / (float)mark[i];
		}
	}
	float g = 1.0f;
	if (n >= 3) {
		for (int a = 1; a < n; a++) {            /* insertion sort, n <= 4 */
			const float v = r[a];
			int b = a - 1;
			while (b >= 0 && r[b] > v) { r[b + 1] = r[b]; b--; }
			r[b + 1] = v;
		}
		g = (n == 3) ? r[1] : 0.5f * (r[1] + r[2]);
		if (g < LYRA_DRIFT_G_MIN || g > LYRA_DRIFT_G_MAX) {
			g = 1.0f;
		}
	}
	if (gain_out) {
		*gain_out = g;
	}
	for (int i = 0; i < 4; i++) {
		const float expect = (float)mark[i] * g;
		const float d = (float)now[i] - expect;
		if (d >= (float)LYRA_DRIFT_TOUCH || d <= -(float)LYRA_DRIFT_TOUCH) {
			return i;
		}
	}
	return -1;
}

#endif
