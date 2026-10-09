/*
 * LYRA-4 fader arming (control map section 3b). Plain C99, no OS, no heap, so the same
 * file builds on a desktop (lyra_uitest.c) and, later, inside the LYRA-4 tree.
 *
 * RULE: a fader never changes a parameter it was not holding.
 *  - Every fader is DISARMED on enter, on every page change and on the shift layer.
 *  - A disarmed fader writes nothing, however it is moved.
 *  - It arms when (a) it is within LYRA_UI_WINDOW of the stored value, or (b) its position
 *    CROSSES the stored value between two valid ticks. After arming it tracks 1:1.
 *  - After a bank change, a fader that is NOT already within the window is also locked
 *    until it has moved LYRA_UI_LOCK from where it was (a finger resting on it, or the
 *    low-pass tail of the last move, is not movement).
 *  - A tick with an invalid scan, or with a ladder button down (rail sag), is discarded:
 *    it does not feed the filter, arm, or write.
 *  - A single-tick jump over LYRA_UI_JUMP is a bad sample: re-seed, apply nothing.
 */
#ifndef LYRA_UI_H
#define LYRA_UI_H

#include <stdint.h>
#include <stdbool.h>

#define LYRA_UI_FADERS   4
#define LYRA_UI_PAGES    4
#define LYRA_UI_BANKS    5      /* 0..3 = pages, 4 = page 0 with the shift key held */
#define LYRA_UI_SHIFT    4

#define LYRA_UI_FULL     3701.0f   /* measured top of travel (LYRA-4 SP1_FADER_FULL)     */
#define LYRA_UI_LP       0.125f    /* one-pole per tick, as LYRA-4                       */
#define LYRA_UI_WINDOW   0.015f    /* arming window, fraction of travel (tunable)       */
#define LYRA_UI_LOCK     0.03f     /* movement that ends the post-change lock           */
#define LYRA_UI_JUMP     0.25f     /* one-tick move that is a bad sample                */

void  lyra_ui_init(const float defaults[LYRA_UI_BANKS][LYRA_UI_FADERS]); /* NULL = 0.5 */

/* Entry to ON. `raw` must come from a VALID scan (scan until valid first). Disarms all. */
void  lyra_ui_enter(const uint16_t raw[LYRA_UI_FADERS]);

/* Page 0..3. Disarms all faders. No-op if unchanged. */
void  lyra_ui_set_page(int page);

/* One control tick. Returns a bitmask of faders whose stored value changed this tick. */
uint32_t lyra_ui_tick(const uint16_t raw[LYRA_UI_FADERS], bool scan_valid,
		      bool ladder_active, bool shift_held);

int   lyra_ui_page(void);
int   lyra_ui_bank(void);                       /* page, or LYRA_UI_SHIFT              */
float lyra_ui_value(int bank, int fader);       /* stored value 0..1                    */
bool  lyra_ui_armed(int fader);                 /* LED: blink slowly when false         */
float lyra_ui_pos(int fader);                   /* filtered fader position 0..1         */

#endif
