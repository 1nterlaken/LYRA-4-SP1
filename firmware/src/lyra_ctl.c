#include "lyra_ctl.h"
#include <string.h>

static float params[LYRA_P_COUNT];
static float pub_prev[LYRA_P_COUNT];
static bool  quantize, quantize_prev;
static uint32_t phase_ms;
/* Unarmed-fader touch detection, for the LEDs only: a T blinks only while its fader is being
 * moved without being armed. Reference position + a hold timer; never feeds the parameters. */
#define TOUCH_DELTA   0.010f    /* 1 % of travel from the reference counts as movement */
#define TOUCH_HOLD_MS 1200u     /* keep blinking this long after the last movement     */
static float    touch_ref[4];
static uint32_t touch_ms[4];
static int      touch_bank = -1;

/* Voice trigger mode. LATCH: T toggles. MOMENTARY: sounds while held ("••" + FFWD). */
static int      mode;
static uint8_t  mom[4];            /* bit0: T held, bit1: PLAY held */

/* Defaults: the LIRA-4 patch's, except the delay feedback (see LYRA4_CONTROL_MAP.md 3c). */
#define DEF_DFB 0.30f
static const float kDefaults[LYRA_UI_BANKS][LYRA_UI_FADERS] = {
	{ 0.4f, 0.6f, 0.5f, 0.5f },      /* page 0: tune 1-4                    */
	{ 0.0f, 0.0f, 0.5f, 0.5f },      /* page 1: mod12, mod34, sharp12, 34   */
	{ 0.5f, DEF_DFB, 0.5f, 0.1f },   /* page 2: time, feedback, mix, mod    */
	{ 0.6f, 0.7f, 0.5f, 0.5f },      /* page 3: lfo A, lfo B, drive, dmix   */
	{ 0.5f, 0.0f, 0.5f, 0.5f },      /* page 0 + "••": pitch, hold, -, -    */
};

static void set_defaults(void)
{
	memset(params, 0, sizeof(params));
	params[LYRA_P_SOURCE12] = 2.0f;        /* the other pair */
	params[LYRA_P_SOURCE34] = 2.0f;
	params[LYRA_P_VOLUME] = 1.0f;          /* output level is LYRA-4' VOL+/VOL- */
	params[LYRA_P_DSOURCE] = 2.0f;
}

void lyra_ctl_init(void)
{
	set_defaults();
	lyra_ui_init(kDefaults);
	quantize = quantize_prev = false;
	phase_ms = 0;
	touch_bank = -1;
	for (int i = 0; i < 4; i++) touch_ms[i] = 0u;
	mode = LYRA_MODE_LATCH;
	for (int i = 0; i < 4; i++) mom[i] = 0u;
	memcpy(pub_prev, params, sizeof(params));
}

void lyra_ctl_enter(const uint16_t raw[4])
{
	/* Voices are off on every entry to ON; toggles keep their state. */
	for (int i = 0; i < 4; i++) { params[LYRA_P_SENSOR1 + i] = 0.0f; mom[i] = 0u; }
	lyra_ui_enter(raw);
	touch_bank = -1;
}

int lyra_ctl_mode(void) { return mode; }

static void voice_on(int i) { params[LYRA_P_SENSOR1 + i] = 1.0f; }
static void voice_off(int i) { params[LYRA_P_SENSOR1 + i] = 0.0f; }

/* Changing mode silences everything so no voice is left latched or held by the old mode. */
static void set_mode(int m)
{
	mode = m;
	for (int i = 0; i < 4; i++) { voice_off(i); mom[i] = 0u; }
}

static float flip(float v) { return v > 0.5f ? 0.0f : 1.0f; }
static float cycle_src(float v) { int s = (int)(v + 0.5f); return (float)(s == 2 ? 0 : (s == 0 ? 1 : 2)); }

static void fill_from_banks(void)
{
	for (int i = 0; i < 4; i++) {
		params[LYRA_P_TUNE1 + i] = lyra_ui_value(0, i);
	}
	params[LYRA_P_MOD12]   = lyra_ui_value(1, 0);
	params[LYRA_P_MOD34]   = lyra_ui_value(1, 1);
	params[LYRA_P_SHARP12] = lyra_ui_value(1, 2);
	params[LYRA_P_SHARP34] = lyra_ui_value(1, 3);
	params[LYRA_P_DTIME]   = lyra_ui_value(2, 0);
	params[LYRA_P_DFB]     = lyra_ui_value(2, 1);
	params[LYRA_P_DMIXD]   = lyra_ui_value(2, 2);
	params[LYRA_P_DMOD]    = lyra_ui_value(2, 3);
	params[LYRA_P_LFOA]    = lyra_ui_value(3, 0);
	params[LYRA_P_LFOB]    = lyra_ui_value(3, 1);
	params[LYRA_P_DRIVE]   = lyra_ui_value(3, 2);
	params[LYRA_P_DMIX]    = lyra_ui_value(3, 3);
	params[LYRA_P_PITCH]   = lyra_ui_value(LYRA_UI_SHIFT, 0);
	params[LYRA_P_HOLD]    = lyra_ui_value(LYRA_UI_SHIFT, 1);
}

static void buttons(bool fn, uint32_t pressed, uint32_t released)
{
	const int pg = lyra_ui_page();
	const uint32_t tm[4] = { LYRA_B_T1, LYRA_B_T2, LYRA_B_T3, LYRA_B_T4 };

	/* Momentary releases count wherever you are: a held voice must always be able to close. */
	for (int i = 0; i < 4; i++) {
		if ((released & tm[i]) && (mom[i] & 1u)) mom[i] &= (uint8_t)~1u;
		if ((released & LYRA_B_PLAY) && (mom[i] & 2u)) mom[i] &= (uint8_t)~2u;
		if (mode == LYRA_MODE_MOMENTARY && mom[i] == 0u && params[LYRA_P_SENSOR1 + i] > 0.5f
		    && ((released & tm[i]) || (released & LYRA_B_PLAY))) voice_off(i);
	}

	if (fn && pg == 0 && (pressed & LYRA_B_FFWD)) {
		set_mode(mode == LYRA_MODE_MOMENTARY ? LYRA_MODE_LATCH : LYRA_MODE_MOMENTARY);
	}

	if (!fn) {
		if (pressed & LYRA_B_RWD)  lyra_ui_set_page((pg + LYRA_UI_PAGES - 1) % LYRA_UI_PAGES);
		if (pressed & LYRA_B_FFWD) lyra_ui_set_page((pg + 1) % LYRA_UI_PAGES);
	}
	if ((pressed & LYRA_B_PLAY) && !fn) {
		if (mode == LYRA_MODE_MOMENTARY) {
			for (int i = 0; i < 4; i++) { mom[i] |= 2u; voice_on(i); }
		} else {
			bool all_on = true;
			for (int i = 0; i < 4; i++) if (params[LYRA_P_SENSOR1 + i] < 0.5f) all_on = false;
			for (int i = 0; i < 4; i++) params[LYRA_P_SENSOR1 + i] = all_on ? 0.0f : 1.0f;
		}
	}
	const uint32_t t[4] = { LYRA_B_T1, LYRA_B_T2, LYRA_B_T3, LYRA_B_T4 };
	for (int i = 0; i < 4; i++) {
		if (!(pressed & t[i])) continue;
		if (fn) {
			if (pg != 0) continue;
			switch (i) {
			case 0: params[LYRA_P_FAST12] = flip(params[LYRA_P_FAST12]); break;
			case 1: params[LYRA_P_FAST34] = flip(params[LYRA_P_FAST34]); break;
			case 2: params[LYRA_P_VIBRATO] = flip(params[LYRA_P_VIBRATO]); break;
			default: quantize = !quantize; break;
			}
			continue;
		}
		switch (pg) {
		case 0:
				if (mode == LYRA_MODE_MOMENTARY) { mom[i] |= 1u; voice_on(i); }
				else { params[LYRA_P_SENSOR1 + i] = flip(params[LYRA_P_SENSOR1 + i]); }
				break;
		case 1:
			if (i == 0) params[LYRA_P_SOURCE12] = cycle_src(params[LYRA_P_SOURCE12]);
			if (i == 1) params[LYRA_P_SOURCE34] = cycle_src(params[LYRA_P_SOURCE34]);
			if (i == 2) params[LYRA_P_TOTALFB] = flip(params[LYRA_P_TOTALFB]);
			break;
		case 2:
			if (i == 0) params[LYRA_P_DWAVE] = flip(params[LYRA_P_DWAVE]);
			if (i == 1) params[LYRA_P_DSOURCE] = cycle_src(params[LYRA_P_DSOURCE]);
			break;
		default:
			if (i == 0) params[LYRA_P_LFOLINK] = flip(params[LYRA_P_LFOLINK]);
			if (i == 1) params[LYRA_P_LFOANDOR] = flip(params[LYRA_P_LFOANDOR]);
			break;
		}
	}
}

bool lyra_ctl_tick(uint32_t dt_ms, const uint16_t raw[4], bool scan_valid, bool ladder_active,
		   bool fn_held, uint32_t pressed, uint32_t released)
{
	phase_ms = (phase_ms + dt_ms) % 800u;
	(void)lyra_ui_tick(raw, scan_valid, ladder_active, fn_held);
	/* LED-only touch tracking. */
	if (lyra_ui_bank() != touch_bank) {
		touch_bank = lyra_ui_bank();
		for (int i = 0; i < 4; i++) { touch_ref[i] = lyra_ui_pos(i); touch_ms[i] = 0u; }
	}
	for (int i = 0; i < 4; i++) {
		const float d = lyra_ui_pos(i) - touch_ref[i];
		if (lyra_ui_armed(i)) {
			touch_ref[i] = lyra_ui_pos(i);
			touch_ms[i] = 0u;
		} else if (d > TOUCH_DELTA || d < -TOUCH_DELTA) {
			touch_ref[i] = lyra_ui_pos(i);
			touch_ms[i] = TOUCH_HOLD_MS;
		} else if (touch_ms[i] > 0u) {
			touch_ms[i] = touch_ms[i] > dt_ms ? touch_ms[i] - dt_ms : 0u;
		}
	}
	fill_from_banks();
	/* A ladder press is exactly when a button edge arrives; the edge itself is real. */
	buttons(fn_held, pressed, released);
	const bool changed = memcmp(pub_prev, params, sizeof(params)) != 0 || quantize != quantize_prev;
	memcpy(pub_prev, params, sizeof(params));
	quantize_prev = quantize;
	return changed;
}

const float *lyra_ctl_params(void) { return params; }
bool lyra_ctl_quantize(void) { return quantize; }

/* Three-way source selector (2 = default source, 0 = alternative source, 1 = off):
 * bright = default source, dim = alternative source, dark = off. */
static uint8_t tri_level(float v)
{
	const int s = (int)(v + 0.5f);
	return s == 2 ? LYRA_TRK_ON : (s == 0 ? LYRA_TRK_MID : 0u);
}

static uint8_t bin_level(float v) { return v > 0.5f ? LYRA_TRK_ON : 0u; }

/* Steady level shown by T1-T4 on the current page.
 *   page 1: voice on/off.
 *   page 2: T1, T2 source (other pair bright / LFO-or-feedback dim / off dark); T3 total feedback.
 *   page 3: T1 delay waveform; T2 delay source (LFO bright / own tap dim / off dark).
 *   page 4: T1 LFO link; T2 LFO and/or. */
static uint8_t track_level(int i)
{
	switch (lyra_ui_page()) {
	case 0: return bin_level(params[LYRA_P_SENSOR1 + i]);
	case 1: if (i == 0) return tri_level(params[LYRA_P_SOURCE12]);
		if (i == 1) return tri_level(params[LYRA_P_SOURCE34]);
		if (i == 2) return bin_level(params[LYRA_P_TOTALFB]);
		return 0u;
	case 2: if (i == 0) return bin_level(params[LYRA_P_DWAVE]);
		if (i == 1) return tri_level(params[LYRA_P_DSOURCE]);
		return 0u;
	default: if (i == 0) return bin_level(params[LYRA_P_LFOLINK]);
		if (i == 1) return bin_level(params[LYRA_P_LFOANDOR]);
		return 0u;
	}
}

void lyra_ctl_leds(uint8_t track[4], uint8_t play[4])
{
	const bool phase = phase_ms < 400u;
	if (lyra_ui_bank() == LYRA_UI_SHIFT) {
		/* "••" held on page 1: T1-T4 are the four switches, shown steady. Only the pitch and
		 * hold faders (0, 1) are live here; faders 2 and 3 are unused and never signal. */
		const bool sw[4] = { params[LYRA_P_FAST12] > 0.5f, params[LYRA_P_FAST34] > 0.5f,
				     params[LYRA_P_VIBRATO] > 0.5f, quantize };
		for (int i = 0; i < 4; i++) track[i] = sw[i] ? LYRA_TRK_ON : 0u;
		for (int i = 0; i < 4; i++) play[i] = 0u;
		/* Page LED (top) dims; it blinks only while an unarmed live fader (pitch, hold) is
		 * being moved. The second side LED shows momentary mode. */
		bool waiting = false;
		for (int i = 0; i <= 1; i++) if (!lyra_ui_armed(i) && touch_ms[i] > 0u) waiting = true;
		play[3] = waiting ? (phase ? LYRA_SIDE_DIM : LYRA_SIDE_LOW) : LYRA_SIDE_DIM;
		play[2] = (mode == LYRA_MODE_MOMENTARY) ? LYRA_SIDE_FULL : 0u;
		return;
	}
	for (int i = 0; i < 4; i++) {
		const uint8_t lvl = track_level(i);
		if (lyra_ui_armed(i) || touch_ms[i] == 0u) {
			track[i] = lvl;
		} else if (lvl >= LYRA_TRK_ON) {
			/* Unarmed fader being moved: slow blink. Bright states blink bright/low; dim and
			 * dark states blink their level (at least the dark-state glimmer) and dark. */
			track[i] = phase ? LYRA_TRK_ON : LYRA_TRK_ON_LO;
		} else {
			track[i] = phase ? (lvl > LYRA_TRK_OFF_HI ? lvl : LYRA_TRK_OFF_HI) : 0u;
		}
	}
	for (int i = 0; i < 4; i++) play[i] = 0u;
	/* Page 1 is the TOP side LED. LED index 0 is the "••" end and index 3 is next to PLAY, so
	 * the page order runs from index 3 down to index 0 (user: page 1 = top, page 4 = bottom). */
	play[3 - lyra_ui_page()] = (lyra_ui_bank() == LYRA_UI_SHIFT) ? LYRA_SIDE_DIM : LYRA_SIDE_FULL;
}
