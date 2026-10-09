/*
 * LYRA-4 — LED rows on hardware PWM, with a tick-driven fade engine.
 *
 * Two rows of four, each on its own nRF PWM peripheral:
 *   TRACK row (PWM2) — above buttons T1-T4. The "model row": Plaits model
 *                      display, and the shutdown animation.
 *   PLAY  row (PWM3) — the side row. The dB meter and the clock.
 *                      Index 0 is the "••" end, index 3 is next to PLAY. The
 *                      hardware channels run the other way; sp1_led.c flips them.
 *
 * Levels are 0-255 PERCEPTUAL, not duty cycle. sp1_led.c applies a gamma table,
 * because perceived brightness is roughly the square of duty and a linear ramp
 * looks wrong -- it appears to rush the bottom end and stall at the top.
 *
 * Why hardware PWM: it costs no CPU. sp1-tape-looper drives these pins as raw
 * GPIO with a software PWM and had to promote its LED timer to a zero-latency
 * IRQ to stop flicker at low duty; that interrupt would compete with the 1333-
 * cycle audio deadline from M2 on. Do not reintroduce software PWM.
 */
#ifndef SP1_LED_H
#define SP1_LED_H

#include <stdint.h>
#include <stdbool.h>

enum sp1_led_row {
	SP1_ROW_TRACK = 0,   /* model row, above T1-T4 */
	SP1_ROW_PLAY  = 1,   /* side row, VU meter     */
};

#define SP1_LEDS_PER_ROW 4

/* Bring up both PWM peripherals. IDEMPOTENT -- safe to call repeatedly; the second
 * and later calls return immediately.
 *
 * The PWM devices are marked `zephyr,deferred-init` in the board DTS, so nothing
 * touches the LED pins until this runs. That is deliberate: Zephyr would otherwise
 * initialise them pre-main, pinctrl would claim the pins, and the peripheral would
 * drive them to its idle level -- which with `nordic,invert` LIGHTS the LEDs -- for
 * the few milliseconds until our first duty write. That was the tap-flicker: a
 * brief press of "••" woke the device, flashed every LED during init, and went
 * straight back to sleep. Deferring init makes it structurally impossible, because
 * a tap never gets far enough to call this.
 *
 * Returns 0 on success. On failure the calls below become no-ops rather than
 * faulting -- a dead LED must never take the device down, because the power-off
 * path has to keep working. */
int sp1_led_init(void);

/* Set a LED immediately, cancelling any fade in progress on it. 0-255. */
void sp1_led_set(enum sp1_led_row row, int i, uint8_t level);

/* Fade a LED to `level` over `ms`. Stepped from sp1_led_tick(); a fade already
 * running on that LED is replaced. ms == 0 is equivalent to sp1_led_set(). */
void sp1_led_fade(enum sp1_led_row row, int i, uint8_t level, uint16_t ms);

/* Fade a whole row together. */
void sp1_led_fade_row(enum sp1_led_row row, uint8_t level, uint16_t ms);

/* ---- one continuous bar across a row (M4c) ----
 * `level` 0-255 fills from index 0 toward index 3; `brightness` scales the whole thing.
 * PWM gives the partly-filled LED a fractional value, so four LEDs read as one bar rather
 * than four steps. STANDBY's charge bar and the "••"-tap battery display are the same
 * call, so they cannot drift apart. */
void sp1_led_bar(enum sp1_led_row row, uint8_t level, uint8_t brightness);

/* Advance every active fade. Call once per control tick with the tick period in
 * milliseconds. At an 8 ms tick a 150 ms fade is ~19 steps and a 500 ms fade
 * ~63 -- visually smooth with the gamma curve applied, so there is no need to
 * move this onto a PWM DMA sequence. */
void sp1_led_tick(uint32_t elapsed_ms);

/* True while any fade is still running. */
bool sp1_led_fading(void);

/* Everything dark, immediately, all fades cancelled. Used before SYSTEM_OFF:
 * it freezes GPIO levels, so anything still lit stays lit into sleep. */
void sp1_leds_all_off(void);

#endif /* SP1_LED_H */
