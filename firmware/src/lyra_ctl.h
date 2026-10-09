/*
 * LYRA-4 control layer: pages, latching voices, toggles, LEDs, parameter vector.
 * Plain C99, no OS. Sits on lyra_ui (fader arming). The LYRA-4 glue (main.c) feeds it
 * button edges and fader reads and publishes `params` to the audio thread.
 *
 * Parameter ids below MIRROR lyra4::Param (lyra4_engine.h); sp1_lyra.cc static_asserts that.
 * Control map: LYRA4_CONTROL_MAP.md. VOL+/VOL- (output level) and the "••" power gestures
 * stay in LYRA-4' main loop and are not handled here.
 */
#ifndef LYRA_CTL_H
#define LYRA_CTL_H

#include "lyra_ui.h"

enum {
	LYRA_P_TUNE1 = 0, LYRA_P_TUNE2, LYRA_P_TUNE3, LYRA_P_TUNE4, LYRA_P_PITCH,
	LYRA_P_MOD12, LYRA_P_MOD34, LYRA_P_SHARP12, LYRA_P_SHARP34,
	LYRA_P_SOURCE12, LYRA_P_SOURCE34, LYRA_P_TOTALFB,
	LYRA_P_HOLD, LYRA_P_VIBRATO, LYRA_P_FAST12, LYRA_P_FAST34,
	LYRA_P_SENSOR1, LYRA_P_SENSOR2, LYRA_P_SENSOR3, LYRA_P_SENSOR4,
	LYRA_P_VOLUME, LYRA_P_DRIVE, LYRA_P_DMIX,
	LYRA_P_DWAVE, LYRA_P_DSOURCE,
	LYRA_P_DMOD, LYRA_P_DFB, LYRA_P_DMIXD, LYRA_P_DTIME,
	LYRA_P_LFOA, LYRA_P_LFOB, LYRA_P_LFOLINK, LYRA_P_LFOANDOR,
	LYRA_P_COUNT
};

/* Button edges (pressed / released this tick; PLAY and T1-T4 matter for `released`). */
#define LYRA_B_PLAY 0x01u
#define LYRA_B_T1   0x02u
#define LYRA_B_T2   0x04u
#define LYRA_B_T3   0x08u
#define LYRA_B_T4   0x10u
#define LYRA_B_RWD  0x20u
#define LYRA_B_FFWD 0x40u

/* Voice trigger modes ("••" + FFWD = momentary; the same press again = latch). */
enum { LYRA_MODE_LATCH = 0, LYRA_MODE_MOMENTARY };
int   lyra_ctl_mode(void);

/* Quantized-tuning switch (page 0, "••" + T4). Not an engine Param: applied via Options. */
void  lyra_ctl_init(void);
void  lyra_ctl_enter(const uint16_t raw[4]);    /* entry to ON: voices off, faders disarmed */
/* One control tick. Returns true if `params` or `quantize` changed. */
bool  lyra_ctl_tick(uint32_t dt_ms, const uint16_t raw[4], bool scan_valid, bool ladder_active,
		    bool fn_held, uint32_t pressed, uint32_t released);
const float *lyra_ctl_params(void);
bool  lyra_ctl_quantize(void);
/* Side (play) row levels, 0..255. The page indicator and the mode LED are deliberately
 * dim: they are always on. The track row keeps full range. */
#define LYRA_SIDE_FULL 100u
#define LYRA_SIDE_DIM   45u     /* shift layer */
#define LYRA_SIDE_LOW   10u     /* low half of the "touching an unpicked fader" blink */
/* Track row (T1-T4) levels. Half the old full-brightness; the shutdown animation is untouched. */
#define LYRA_TRK_ON     130u
#define LYRA_TRK_MID     45u    /* three-way selectors: the alternative source */
#define LYRA_TRK_ON_LO   50u    /* low half of the blink on an "on" T */
#define LYRA_TRK_OFF_HI  40u    /* high half of the blink on an "off" T */
/* LED levels 0..255 for the track row (T1-T4) and the play row (page indicator). */
void  lyra_ctl_leds(uint8_t track[4], uint8_t play[4]);

#endif
