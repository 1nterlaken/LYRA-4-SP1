/*
 * LYRA-4 — LED rows on hardware PWM. See sp1_led.h.
 *
 * Pin order here is defined by the pinctrl channel assignment in
 * boards/teenageengineering/stem_player/stem_player-pinctrl.dtsi. On the TRACK row
 * PWM channel N is LED index N (T1..T4). On the PLAY row the index is REVERSED in
 * apply(): index 0 is the "••" end, channel 0 is the PLAY end. Changing the pinctrl
 * without reading apply() silently scrambles the display.
 */
#include "sp1_led.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

/* ~977 Hz: well above flicker perception, and 1024 cycles gives 10-bit duty
 * resolution, which is what the gamma table below expects. sp1-midi uses the
 * same 1024 us period. */
#define PWM_PERIOD_CYCLES  1024u

static const struct device *pwm_dev[2];
static bool ready;

/* Perceptual level (0-255) -> PWM duty (0-1023), duty = (level/255)^2.2.
 * Without this a linear ramp appears to rush the bottom end and stall near the
 * top; with it, a fade reads as even. */
static const uint16_t gamma_lut[256] = {
	   0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1,    1,
	   1,    1,    2,    2,    2,    3,    3,    3,    4,    4,    5,    5,
	   6,    6,    7,    7,    8,    9,    9,   10,   11,   11,   12,   13,
	  14,   15,   16,   16,   17,   18,   19,   20,   21,   23,   24,   25,
	  26,   27,   28,   30,   31,   32,   34,   35,   36,   38,   39,   41,
	  42,   44,   46,   47,   49,   51,   52,   54,   56,   58,   60,   61,
	  63,   65,   67,   69,   71,   73,   76,   78,   80,   82,   84,   87,
	  89,   91,   94,   96,   98,  101,  103,  106,  109,  111,  114,  117,
	 119,  122,  125,  128,  130,  133,  136,  139,  142,  145,  148,  151,
	 155,  158,  161,  164,  167,  171,  174,  177,  181,  184,  188,  191,
	 195,  198,  202,  206,  209,  213,  217,  221,  225,  228,  232,  236,
	 240,  244,  248,  252,  257,  261,  265,  269,  274,  278,  282,  287,
	 291,  295,  300,  304,  309,  314,  318,  323,  328,  333,  337,  342,
	 347,  352,  357,  362,  367,  372,  377,  382,  387,  393,  398,  403,
	 408,  414,  419,  425,  430,  436,  441,  447,  452,  458,  464,  470,
	 475,  481,  487,  493,  499,  505,  511,  517,  523,  529,  535,  542,
	 548,  554,  561,  567,  573,  580,  586,  593,  599,  606,  613,  619,
	 626,  633,  640,  647,  653,  660,  667,  674,  681,  689,  696,  703,
	 710,  717,  725,  732,  739,  747,  754,  762,  769,  777,  784,  792,
	 800,  807,  815,  823,  831,  839,  847,  855,  863,  871,  879,  887,
	 895,  903,  912,  920,  928,  937,  945,  954,  962,  971,  979,  988,
	 997, 1005, 1014, 1023
};

struct fade {
	uint8_t  from;
	uint8_t  to;
	uint8_t  current;
	uint16_t total_ms;
	uint16_t elapsed_ms;
	bool     active;
};

static struct fade fades[2][SP1_LEDS_PER_ROW];

static void apply(enum sp1_led_row row, int i, uint8_t level)
{
	if (!ready || !pwm_dev[row]) {
		return;
	}
	/* ---- the play row is wired PLAY-end first ----
	 * PWM3 channel 0 is P1.13, which is the LED next to PLAY (confirmed on hardware,
	 * M2: the meter came out reversed). Every play-row user speaks in "from the ••
	 * end" order -- the dB meter, the STANDBY charge bar, the clock -- so flip it
	 * here, once, rather than in each of them. Logical 0 = the "••" end. */
	const uint32_t ch = (row == SP1_ROW_PLAY)
		? (uint32_t)(SP1_LEDS_PER_ROW - 1 - i) : (uint32_t)i;

	/* pinctrl marks these channels nordic,invert, so a larger pulse is a
	 * brighter LED; the driver handles the inversion. */
	(void)pwm_set_cycles(pwm_dev[row], ch, PWM_PERIOD_CYCLES,
			     gamma_lut[level], 0);
}

int sp1_led_init(void)
{
	if (ready) {
		return 0;      /* idempotent -- see the header */
	}

	pwm_dev[SP1_ROW_TRACK] = DEVICE_DT_GET(DT_NODELABEL(pwm2));
	pwm_dev[SP1_ROW_PLAY]  = DEVICE_DT_GET(DT_NODELABEL(pwm3));

	for (int r = 0; r < 2; r++) {
		/* These are `zephyr,deferred-init` in the DTS, so nothing has run
		 * for them yet and pinctrl has not claimed the pins. This call is
		 * what applies pinctrl -- which is exactly why it must not happen
		 * until we have committed to booting. */
		if (device_init(pwm_dev[r]) < 0) {
			pwm_dev[r] = NULL;
			return -1;
		}
		if (!device_is_ready(pwm_dev[r])) {
			/* Leave `ready` false: every call below becomes a no-op.
			 * A dead LED row must never prevent power-off. */
			pwm_dev[r] = NULL;
			return -1;
		}
	}
	ready = true;

	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
			fades[r][i].active = false;
			fades[r][i].current = 0;
			apply((enum sp1_led_row)r, i, 0);
		}
	}
	return 0;
}

void sp1_led_set(enum sp1_led_row row, int i, uint8_t level)
{
	if (i < 0 || i >= SP1_LEDS_PER_ROW) {
		return;
	}
	fades[row][i].active = false;
	fades[row][i].current = level;
	apply(row, i, level);
}

void sp1_led_fade(enum sp1_led_row row, int i, uint8_t level, uint16_t ms)
{
	if (i < 0 || i >= SP1_LEDS_PER_ROW) {
		return;
	}
	if (ms == 0u) {
		sp1_led_set(row, i, level);
		return;
	}
	struct fade *f = &fades[row][i];
	f->from       = f->current;
	f->to         = level;
	f->total_ms   = ms;
	f->elapsed_ms = 0u;
	f->active     = true;
}

void sp1_led_bar(enum sp1_led_row row, uint8_t level, uint8_t brightness)
{
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		const uint32_t lo = (uint32_t)i * 255u / SP1_LEDS_PER_ROW;
		const uint32_t hi = (uint32_t)(i + 1) * 255u / SP1_LEDS_PER_ROW;
		uint32_t seg;

		if (level >= hi) {
			seg = 255u;
		} else if (level > lo) {
			seg = ((uint32_t)(level - lo) * 255u) / (hi - lo);
		} else {
			seg = 0u;
		}
		sp1_led_set(row, i, (uint8_t)((seg * (uint32_t)brightness) / 255u));
	}
}

void sp1_led_fade_row(enum sp1_led_row row, uint8_t level, uint16_t ms)
{
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		sp1_led_fade(row, i, level, ms);
	}
}

void sp1_led_tick(uint32_t elapsed_ms)
{
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
			struct fade *f = &fades[r][i];
			if (!f->active) {
				continue;
			}
			f->elapsed_ms += (uint16_t)elapsed_ms;
			if (f->elapsed_ms >= f->total_ms) {
				f->current = f->to;
				f->active  = false;
			} else {
				/* Interpolate in PERCEPTUAL space, then gamma is
				 * applied on the way to the hardware. Interpolating
				 * duty directly would undo the correction. */
				const int32_t span = (int32_t)f->to - (int32_t)f->from;
				f->current = (uint8_t)((int32_t)f->from +
					(span * (int32_t)f->elapsed_ms) / (int32_t)f->total_ms);
			}
			apply((enum sp1_led_row)r, i, f->current);
		}
	}
}

bool sp1_led_fading(void)
{
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
			if (fades[r][i].active) {
				return true;
			}
		}
	}
	return false;
}

void sp1_leds_all_off(void)
{
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
			sp1_led_set((enum sp1_led_row)r, i, 0);
		}
	}
}
