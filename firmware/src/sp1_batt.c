/* LYRA-4 — battery voltage and charge estimate. See sp1_batt.h. */
#include "sp1_batt.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>

/* The battery is io-channel index 0 on zephyr_user. Faders APPEND after it when
 * they arrive, so this index never has to move. */
#define BATT_IO_IDX 0

static const struct adc_dt_spec batt_spec =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), BATT_IO_IDX);

/* Full scale: gain 1/6 with the 0.6 V internal reference => 3.6 V across 12 bits.
 * These MUST match the channel properties in firmware/app.overlay. */
#define ADC_FULL_SCALE_MV  3600u
#define ADC_MAX_COUNTS     4095u

/* SP1_BATT_DIVIDER_MILLI lives in sp1_batt.h so the console can report what we
 * are currently assuming alongside what it measured. A /2 divider is the likely
 * design: it puts a 4.20 V cell at 2.10 V, well inside the 3.6 V window. */

static uint16_t raw_last;
static uint16_t mv_last;
static bool     have_sample;
static bool     adc_ok;

/* Smoothed millivolts for display. Raw readings scatter +/-8 counts (~15 mV at the
 * cell), which made the computed level jitter between 247 and 255 -- visible as the
 * top LED of the charge bar wobbling. Hardware oversampling below takes most of it
 * out; this takes the rest. */
static uint32_t mv_smooth_q4;      /* millivolts, Q4 fixed point */
#define SMOOTH_SHIFT 3             /* first-order IIR, ~8-sample time constant */

int sp1_batt_init(void)
{
	if (!adc_is_ready_dt(&batt_spec)) {
		return -1;
	}
	if (adc_channel_setup_dt(&batt_spec) < 0) {
		return -1;
	}
	adc_ok = true;
	return 0;
}

void sp1_batt_sample(void)
{
	if (!adc_ok) {
		return;
	}

	int16_t buf = 0;
	struct adc_sequence seq = {
		.buffer      = &buf,
		.buffer_size = sizeof(buf),
	};
	if (adc_sequence_init_dt(&batt_spec, &seq) < 0) {
		return;
	}
	/* Hardware oversampling: the SAADC averages 2^n conversions for us, which is
	 * free compared with doing it in software.
	 *
	 * ⚠️ M1d NOTE: nrfx cannot oversample a sequence containing MORE THAN ONE
	 * channel. When the faders arrive, keep the battery in its own single-channel
	 * sequence rather than folding it into a scan, or this silently stops working. */
	seq.oversampling = 4;      /* 2^4 = 16 conversions averaged */
	if (adc_read(batt_spec.dev, &seq) < 0) {
		return;
	}

	if (buf < 0) {
		buf = 0;          /* single-ended; a negative code is noise */
	}
	raw_last = (uint16_t)buf;
	/* raw -> mV at the pin -> mV at the cell. Ordered to keep the intermediate
	 * inside uint32: raw (<=4095) * 3600 is ~14.7e6, fine. */
	const uint32_t pin_mv = ((uint32_t)raw_last * ADC_FULL_SCALE_MV)
				/ ADC_MAX_COUNTS;
	const uint32_t mv_now = (pin_mv * SP1_BATT_DIVIDER_MILLI) / 1000u;

	if (!have_sample) {
		mv_smooth_q4 = mv_now << 4;      /* seed, do not ramp from zero */
	} else {
		mv_smooth_q4 += ((int32_t)(mv_now << 4) - (int32_t)mv_smooth_q4)
				>> SMOOTH_SHIFT;
	}
	mv_last = (uint16_t)(mv_smooth_q4 >> 4);
	have_sample = true;
}

uint16_t sp1_batt_raw(void) { return raw_last; }
uint16_t sp1_batt_mv(void)  { return mv_last; }
bool sp1_batt_valid(void)   { return have_sample; }

/* Resting Li-ion voltage -> state of charge. Voltage is a poor proxy for charge
 * on its own -- the curve is flat through the middle -- so this is a coarse
 * lookup, which is all a four-LED bar can express anyway. */
struct soc_point { uint16_t mv; uint8_t pct; };
static const struct soc_point soc_curve[] = {
	{ 3300,   0 }, { 3500,   5 }, { 3620,  10 }, { 3700,  20 },
	{ 3740,  30 }, { 3780,  40 }, { 3820,  50 }, { 3880,  60 },
	{ 3950,  70 }, { 4000,  80 }, { 4100,  90 }, { 4200, 100 },
};
#define SOC_POINTS (sizeof(soc_curve) / sizeof(soc_curve[0]))

uint8_t sp1_batt_level(void)
{
	if (!have_sample) {
		return 128u;    /* neutral midpoint rather than a scary empty bar */
	}
	const uint16_t mv = mv_last;

	if (mv <= soc_curve[0].mv) {
		return 0u;
	}
	if (mv >= soc_curve[SOC_POINTS - 1].mv) {
		return 255u;
	}
	for (size_t i = 1; i < SOC_POINTS; i++) {
		if (mv < soc_curve[i].mv) {
			const struct soc_point *a = &soc_curve[i - 1];
			const struct soc_point *b = &soc_curve[i];
			const uint32_t span_mv  = (uint32_t)(b->mv - a->mv);
			const uint32_t span_pct = (uint32_t)(b->pct - a->pct);
			const uint32_t pct = (uint32_t)a->pct +
				((uint32_t)(mv - a->mv) * span_pct) / span_mv;
			return (uint8_t)((pct * 255u) / 100u);
		}
	}
	return 255u;
}
