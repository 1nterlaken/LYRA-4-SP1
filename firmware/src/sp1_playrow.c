/* LYRA-4 — play row layers. See sp1_playrow.h. */
#include "sp1_playrow.h"
#include "sp1_meter.h"
#include "sp1_led.h"
#include "sp1_ui_timing.h"

static enum sp1_playrow_fg fg_sel = SP1_FG_METER;
static uint8_t  clock_pos;

void sp1_playrow_reset(void)
{
	sp1_meter_reset();
	clock_pos = 0u;
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		sp1_led_set(SP1_ROW_PLAY, i, 0u);
	}
}

void sp1_playrow_set_fg(enum sp1_playrow_fg fg)
{
	fg_sel = fg;
}

/* Scale a full-brightness layer down to the background cap. */
static uint8_t as_bg(uint8_t v)
{
	return (uint8_t)(((uint32_t)v * SP1_ROW_BG_MAX) / 255u);
}

/* One LED: foreground over background.
 *
 * Normally the brighter layer wins. But a background at <= 10 % can never win against
 * a foreground near full, so over a bright meter the clock would vanish. So ABOVE 80 %
 * the background SUBTRACTS instead (Adara, M3): it dims the foreground by up to its
 * own level, i.e. up to 10 %, which shows up as a darker notch travelling through a
 * full bar.
 *
 * The dip FADES IN from nothing at 80 % to the full amount at 100 %, rather than
 * switching on at 80 %. A hard switch would make the LED drop by 10 % the instant the
 * meter crosses 80 % -- on a moving meter, that reads as flicker.
 *
 *   front <= 204            : max(front, back)        (back only shows below it)
 *   front  > 204, back lit  : front - back * (front - 204) / 51
 *   front = 255, back = 26  : 229, i.e. 100 % -> 90 %
 *
 * Continuous at 204 in both directions, and never brighter than the foreground. */
#define DIP_START 204u           /* 80 % of 255 */

static uint8_t composite(uint8_t front, uint8_t back)
{
	if (front > DIP_START && back > 0u) {
		const uint32_t dip = ((uint32_t)back * (front - DIP_START)) /
				     (255u - DIP_START);
		return (uint8_t)(front - dip);
	}
	return front > back ? front : back;
}

void sp1_playrow_clock_step(uint32_t ticks)
{
	clock_pos = (uint8_t)((clock_pos + ticks) % SP1_LEDS_PER_ROW);
}

void sp1_playrow_clock_reset(void)
{
	clock_pos = (uint8_t)(SP1_LEDS_PER_ROW - 1u);   /* the first tick lands on 0 */
}

void sp1_playrow_tick(uint32_t elapsed_ms)
{
	(void)elapsed_ms;

	/* ---- meter layer ---- */
	sp1_meter_tick();
	uint8_t meter[SP1_LEDS_PER_ROW];
	sp1_meter_leds(meter);

	/* ---- composite: brighter wins, background capped ---- */
	for (int i = 0; i < SP1_LEDS_PER_ROW; i++) {
		const uint8_t clk = (i == clock_pos) ? 255u : 0u;
		uint8_t front, back;

		if (fg_sel == SP1_FG_CLOCK) {
			front = clk;
			back  = as_bg(meter[i]);
		} else {
			front = meter[i];
			back  = as_bg(clk);
		}
		sp1_led_set(SP1_ROW_PLAY, i, composite(front, back));
	}
}
