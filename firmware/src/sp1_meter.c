/* LYRA-4 — dB meter on the play row. See sp1_meter.h for the design. */
#include "sp1_meter.h"
#include "sp1_led.h"
#include "sp1_audio.h"
#include "sp1_ui_timing.h"

/* ---- position inside a band, linear in dB ----
 * Entry i covers mantissa (i + 0.5) / 128 of the octave, and holds
 *
 *     round(64 * log2(1 + (i + 0.5) / 128))
 *
 * So a peak halfway through a band IN DECIBELS lands at ~32 -- half brightness -- which
 * is what makes equal dB read as equal brightness. Worst-case error 0.08 dB.
 *
 * Why 128 entries and not 32: entry 0 must be exactly 0. At 32 or 64 entries the first
 * centre-sampled value rounds to 1, so a peak sitting EXACTLY on a band edge (every
 * even tone step is one) lit the next LED at 4/255 instead of leaving it dark, and the
 * whole mapping sat slightly high. After the gamma curve that glint is invisible, but a
 * meter that is right only because a rounding error happens to vanish is a meter that
 * skews its bands, which is the one thing the spec asked it not to do. 128 bytes is
 * the price of being right by construction. Top entry is 64, so the top of one band
 * meets the bottom of the next with no gap.
 *
 * ⚠️ GENERATE THIS, do not type it. An earlier, hand-typed 32-entry version had 25
 * wrong entries, off by up to 6 units. Python:
 *   [round(64*math.log2(1+(i+0.5)/128)) for i in range(128)] */
static const uint8_t LOG2_MANTISSA_64[128] = {
	 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,  7,  8,  9,  9, 10, 11,
	11, 12, 12, 13, 14, 14, 15, 16, 16, 17, 17, 18, 19, 19, 20, 20,
	21, 21, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 28, 28, 29, 29,
	30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37,
	38, 38, 39, 39, 40, 40, 41, 41, 41, 42, 42, 43, 43, 44, 44, 45,
	45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 49, 50, 50, 51, 51, 51,
	52, 52, 53, 53, 54, 54, 54, 55, 55, 56, 56, 56, 57, 57, 57, 58,
	58, 59, 59, 59, 60, 60, 61, 61, 61, 62, 62, 62, 63, 63, 63, 64,
};

/* 20 * log10(2048 / 32768) = -24.08 dBFS: the bottom of the lowest band. */
#define METER_FLOOR 2048u

uint16_t sp1_meter_level_from_peak(uint32_t peak)
{
	if (peak < METER_FLOOR) {
		return 0u;                            /* below -24 dBFS, or silence */
	}
	if (peak > 32767u) {
		peak = 32767u;                        /* |-32768| */
	}

	/* The whole trick: which power-of-two octave the peak sits in, in one CLZ.
	 * peak in [2^14, 2^15) -> clz 17 -> the top band (-6 .. 0 dBFS).
	 * Guarded above: peak >= 2048, so clz is 17..20 and never undefined (clz(0)). */
	const uint32_t lz = (uint32_t)__builtin_clz(peak);
	const uint32_t band_from_top = lz - 17u;          /* 0 (loudest) .. 3 */
	const uint32_t exp = 31u - lz;                    /* peak in [2^exp, 2^exp+1) */
	const uint32_t idx = (peak >> (exp - 7u)) & 127u; /* top seven mantissa bits */

	uint32_t lvl = (3u - band_from_top) * 64u + LOG2_MANTISSA_64[idx];
	if (lvl > SP1_METER_FULL - 1u) {
		lvl = SP1_METER_FULL - 1u;
	}
	return (uint16_t)lvl;
}

uint8_t sp1_meter_led_level(uint16_t level, int i)
{
	/* LED i owns positions [64i, 64i + 64). Below: dark. Above: full. Inside: scaled
	 * to 0..255, and because `level` is already linear in dB, so is the brightness. */
	const int32_t v = ((int32_t)level - 64 * i) * 4;
	if (v <= 0) {
		return 0u;
	}
	return (v >= 255) ? 255u : (uint8_t)v;
}

/* ---- the window: peak of the last WINDOW ticks, no decay. See sp1_meter.h. ---- */
#define WINDOW 3

static uint16_t win[WINDOW];      /* raw peaks, newest overwrites oldest */
static uint8_t  win_pos;
static uint16_t shown_level;
static uint8_t  leds[SP1_LEDS_PER_ROW];

void sp1_meter_reset(void)
{
	(void)sp1_audio_take_peak();              /* discard anything stale */
	for (int k = 0; k < WINDOW; k++) {
		win[k] = 0u;
	}
	win_pos = 0u;
	shown_level = 0u;
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		leds[i] = 0u;
	}
}

void sp1_meter_tick(void)
{
	/* Read-and-clear: the peak of everything the audio thread produced since the
	 * last tick, however many blocks that was. */
	uint32_t p = sp1_audio_take_peak();
	if (p > 32767u) {
		p = 32767u;
	}
	win[win_pos] = (uint16_t)p;
	win_pos = (uint8_t)((win_pos + 1u) % WINDOW);

	uint32_t peak = 0u;
	for (int k = 0; k < WINDOW; k++) {
		if (win[k] > peak) {
			peak = win[k];
		}
	}

	shown_level = sp1_meter_level_from_peak(peak);
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		leds[i] = sp1_meter_led_level(shown_level, i);
	}

	/* The glint: LED 0 never shows less than SP1_METER_GLINT while there is any
	 * signal at or above -48 dBFS, and inside its band it rises from the glint to
	 * full rather than from dark, so the bar stays monotone. */
	if (peak >= SP1_METER_PRESENT) {
		leds[0] = (uint8_t)(SP1_METER_GLINT +
			((uint32_t)leds[0] * (255u - SP1_METER_GLINT)) / 255u);
	}
}

void sp1_meter_leds(uint8_t out[4])
{
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		out[i] = leds[i];
	}
}

uint16_t sp1_meter_level(void)
{
	return shown_level;
}

/* ---- if a wider range is ever wanted ----
 * The band floor (-24 dBFS) is set by four bands of one octave each; the glint covers
 * -48..-24 with a single "something is there" level. For real resolution down there,
 * 12 dB bands (two octaves each) give a -48 dBFS floor with the same one-instruction
 * band lookup: band = (clz - 17) / 2, and the in-band position takes one more mantissa
 * bit. Coarser at the top. Not built. */
