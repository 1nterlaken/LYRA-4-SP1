#include "lyra_ui.h"
#include <math.h>

static float stored[LYRA_UI_BANKS][LYRA_UI_FADERS];
static float pos[LYRA_UI_FADERS];
static float prev[LYRA_UI_FADERS];
static float lock_ref[LYRA_UI_FADERS];
static float lastraw[LYRA_UI_FADERS];
static bool  armed[LYRA_UI_FADERS];
static bool  locked[LYRA_UI_FADERS];
static int   page;
static int   bank;
static bool  seeded;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float to01(uint16_t r) { return clamp01((float)r / LYRA_UI_FULL); }

void lyra_ui_init(const float d[LYRA_UI_BANKS][LYRA_UI_FADERS])
{
	for (int b = 0; b < LYRA_UI_BANKS; b++)
		for (int i = 0; i < LYRA_UI_FADERS; i++)
			stored[b][i] = d ? clamp01(d[b][i]) : 0.5f;
	page = 0; bank = 0; seeded = false;
	for (int i = 0; i < LYRA_UI_FADERS; i++) { pos[i] = prev[i] = lock_ref[i] = 0.0f; armed[i] = false; locked[i] = false; }
}

/* Decide, for the bank now showing, which faders start armed or locked. */
static void disarm_all(void)
{
	for (int i = 0; i < LYRA_UI_FADERS; i++) {
		prev[i] = pos[i];
		lock_ref[i] = pos[i];
		if (fabsf(stored[bank][i] - pos[i]) <= LYRA_UI_WINDOW) {
			/* Already on the value: no jump possible beyond the window. */
			armed[i] = true;
			locked[i] = false;
		} else {
			armed[i] = false;
			locked[i] = true;
		}
	}
}

void lyra_ui_enter(const uint16_t raw[LYRA_UI_FADERS])
{
	for (int i = 0; i < LYRA_UI_FADERS; i++) { pos[i] = lastraw[i] = to01(raw[i]); }
	page = 0; bank = 0; seeded = true;
	disarm_all();
}

void lyra_ui_set_page(int p)
{
	if (p < 0 || p >= LYRA_UI_PAGES || p == page) return;
	page = p; bank = p;
	disarm_all();
}

uint32_t lyra_ui_tick(const uint16_t raw[LYRA_UI_FADERS], bool scan_valid,
		      bool ladder_active, bool shift_held)
{
	if (!seeded) return 0u;

	/* Shift layer exists on page 0 only. Entering or leaving it is a bank change. */
	const int want = (page == 0 && shift_held) ? LYRA_UI_SHIFT : page;
	if (want != bank) { bank = want; disarm_all(); }

	/* Discard: nothing from this tick is allowed to influence anything. */
	if (!scan_valid || ladder_active) return 0u;

	uint32_t ev = 0u;
	for (int i = 0; i < LYRA_UI_FADERS; i++) {
		const float now = to01(raw[i]);
		const float jump = fabsf(now - lastraw[i]);
		lastraw[i] = now;
		if (jump >= LYRA_UI_JUMP) {
			/* A quarter of travel in one tick is not a hand: re-seed, apply nothing. */
			pos[i] = prev[i] = lock_ref[i] = now;
			continue;
		}
		const float filt = pos[i] + LYRA_UI_LP * (now - pos[i]);
		pos[i] = filt;

		float *const s = &stored[bank][i];
		if (locked[i]) {
			if (fabsf(pos[i] - lock_ref[i]) > LYRA_UI_LOCK) locked[i] = false;
			prev[i] = pos[i];
			continue;
		}
		if (armed[i]) {
			if (pos[i] != *s) { *s = pos[i]; ev |= 1u << i; }
			prev[i] = pos[i];
			continue;
		}
		/* Disarmed and unlocked: arm on crossing or on reaching the window. */
		const bool crossed = ((prev[i] - *s) * (pos[i] - *s)) <= 0.0f;
		if (crossed || fabsf(pos[i] - *s) <= LYRA_UI_WINDOW) {
			armed[i] = true;
			*s = pos[i];
			ev |= 1u << i;
		}
		prev[i] = pos[i];
	}
	return ev;
}

int   lyra_ui_page(void)                 { return page; }
int   lyra_ui_bank(void)                 { return bank; }
float lyra_ui_value(int b, int i)        { return stored[b][i]; }
bool  lyra_ui_armed(int i)               { return armed[i]; }
float lyra_ui_pos(int i)                 { return pos[i]; }
