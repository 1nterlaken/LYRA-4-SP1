/* LYRA-4 — guided ladder calibration. See sp1_calib.h for why this exists. */
#include "sp1_calib.h"
#include "sp1_controls.h"
#include "sp1_console.h"
#include "sp1_ui_timing.h"   /* SP1_TICK_MS -- the settle counts derive from it */

#include <zephyr/kernel.h>

/* ---- tuning ----
 * NEED_SAMPLES at the ~56 ms capture cadence is about a third of a second of
 * holding still. The measured plateaus in the first capture had a spread of about 6
 * counts, so TOL of 24 is four times the observed noise: tight enough that a slipping
 * finger does not register, loose enough that a firm press always does. */
/* ---- ⚠️ SAMPLE COUNTS ARE DERIVED FROM THE TICK, NOT WRITTEN AS LITERALS ----
 * The first version hard-coded 6 and 14 and reasoned about them as "a third of a
 * second" and "~0.8 s", sized against the 56 ms console PRINT cadence. But this
 * machine is ticked once per CONTROL tick -- 8 ms -- and sp1_controls_scan() produces
 * a fresh sample just as often. So 6 samples was 48 ms and 14 was 112 ms, roughly
 * 7x shorter than intended.
 *
 * That is the actual reason two partial presses were captured as complete: the
 * "longer settle" meant to let a second finger land was a tenth of a second. Derive
 * the counts from the real period so the intent survives a change to SP1_TICK_MS. */
#define MS_TO_SAMPLES(ms)  (((ms) + SP1_TICK_MS - 1u) / SP1_TICK_MS)

#define SETTLE_SINGLE_MS   300u    /* a single button: a third of a second         */
#define SETTLE_COMBO_MS    800u    /* multi-button: time for every finger to land  */
#define SETTLE_IDLE_MS     150u    /* a release, past contact bounce               */

#define NEED_SAMPLES        MS_TO_SAMPLES(SETTLE_SINGLE_MS)

/* ---- multi-button steps need longer, and a sanity check ----
 * You do not press two buttons at the same instant. The first one lands, holds
 * perfectly still for a third of a second, and a 6-sample detector captures THAT.
 * It happened twice on the 2026-09-19 run: the T1+T2 step recorded 211 (T1 alone,
 * 209) and the T1+T2+T3 step recorded 863 (T1+T3, 864). Both look like clean
 * captures with a spread of 6-7 counts, which is what makes the failure dangerous.
 *
 * Two defences. A longer settle, and -- the real one -- refusing a value that
 * matches something already captured. If the target is T1+T2 and the reading is
 * T1's reading, you are mid-press by definition, whatever its spread. */
#define NEED_SAMPLES_COMBO MS_TO_SAMPLES(SETTLE_COMBO_MS)
#define DUP_TOL            20u     /* "this matches an earlier capture"            */
#define NEED_IDLE_SAMPLES   MS_TO_SAMPLES(SETTLE_IDLE_MS)
#define TOL                24u     /* counts a plateau may wander                  */
#define PRESS_MIN         100u     /* clearly off idle. Lowest single seen: ~209   */
#define IDLE_MAX           60u     /* clearly at idle. Idle measured as exactly 0  */
#define STEP_TIMEOUT_MS 20000u     /* unanswered step -> SKIPPED, move on          */
#define LIVE_PERIOD_MS   1000u     /* "still waiting, here is what I see"          */

/* Fader window for the sag baseline: mid-travel, where a sag is most visible and
 * where the reading is furthest from either end stop. */
#define SAG_LO           1200u
#define SAG_HI           2900u

enum kind {
	K_L0 = 0,      /* a press on the tracks ladder   */
	K_L1,          /* a press on the vol/rocker ladder */
	K_SAG_BASE,    /* park all four faders mid-travel  */
	K_SAG_PRESS,   /* hold something; measure the dip  */
	K_END,
};

struct step {
	enum kind   kind;
	const char *label;
};

/* ---- the script ----
 * Singles first, because those are the ones the UI actually dispatches on. The pairs
 * are here for EXCLUSION rather than for use: `••` is the shift, so the UI never asks
 * for two track buttons at once -- but a fumbled T2 that lands as T2+T3 must not be
 * read as some other single. We cannot exclude a value we have never measured. */
static const struct step steps[] = {
	{ K_L0, "T1" },   { K_L0, "T2" },   { K_L0, "T3" },   { K_L0, "T4" },
	{ K_L0, "PLAY" },

	{ K_L0, "T1+T2" }, { K_L0, "T1+T3" }, { K_L0, "T1+T4" },
	{ K_L0, "T2+T3" }, { K_L0, "T2+T4" }, { K_L0, "T3+T4" },

	/* ⚠️ ALL FOUR three-button combinations, including the two that were missing.
	 * duplicate_of() can only reject a partial press whose value it has already
	 * seen, so an unmeasured proper subset is a hole in the defence: on the
	 * T1+T2+T3+T4 step, pressing T1+T3+T4 first and holding still recorded ~1626
	 * as the four-button value, because nothing within 20 counts had been captured
	 * (the nearest, T2+T3+T4 at 1692, is 66 away). Measuring every subset closes
	 * it. Two extra steps is a cheap price for a table that cannot be wrong. */
	{ K_L0, "T1+T2+T3" }, { K_L0, "T1+T2+T4" },
	{ K_L0, "T1+T3+T4" }, { K_L0, "T2+T3+T4" },
	{ K_L0, "T1+T2+T3+T4" },
	{ K_L0, "PLAY+T1" }, { K_L0, "PLAY+T4" },

	{ K_L1, "VOL+" }, { K_L1, "VOL-" }, { K_L1, "FFWD" }, { K_L1, "RWD" },

	{ K_SAG_BASE,  "faders mid-travel" },
	{ K_SAG_PRESS, "T1"   },
	{ K_SAG_PRESS, "PLAY" },

	{ K_END, NULL },
};
#define NSTEPS ((int)(sizeof(steps) / sizeof(steps[0])) - 1)

struct result {
	uint16_t mean, lo, hi;
	bool     ok;
};
static struct result results[NSTEPS];

enum phase { P_ANNOUNCE = 0, P_WAIT_PRESS, P_WAIT_RELEASE, P_REPORT, P_DONE };

static bool       running;
static int        idx;
static enum phase phase;
static uint32_t   step_ms;
static uint32_t   live_ms;

static uint32_t   stable_n, idle_n;
static uint16_t   ref, lo, hi;
static uint32_t   acc;          /* running sum, for a mean rather than a midpoint */

/* Sag bookkeeping. */
static uint16_t   sag_base[SP1_NUM_FADERS];
static bool       sag_base_ok;
static uint16_t   last_fad[SP1_NUM_FADERS];   /* for "are all four holding still" */
static bool       dup_warned;                 /* one partial-press warning per step */

static uint16_t ladder_for(int i)
{
	return sp1_ladder_raw(steps[i].kind == K_L1 ? 1 : 0);
}

static void stable_reset(void)
{
	stable_n = 0u;
	idle_n = 0u;
	acc = 0u;
	ref = lo = hi = 0u;
}

/* Is this a multi-button target? Cheap: the labels contain a '+'. */
static bool is_combo(const char *label)
{
	for (const char *p = label; *p; p++) {
		if (*p == '+') {
			return true;
		}
	}
	return false;
}

/* Does `v` match a value already captured on the same ladder? If so, the operator
 * has not finished pressing the current target. */
static const char *duplicate_of(uint16_t v)
{
	for (int i = 0; i < idx; i++) {
		if (!results[i].ok) {
			continue;
		}
		if (steps[i].kind != steps[idx].kind) {
			continue;
		}
		const uint16_t c = results[i].mean;
		if ((uint32_t)(v > c ? v - c : c - v) <= DUP_TOL) {
			return steps[i].label;
		}
	}
	return NULL;
}

/* Feed one sample into the plateau detector. Returns true once NEED_SAMPLES
 * consecutive samples have stayed within TOL of the first. */
static bool stable_feed(uint16_t v, uint32_t need)
{
	if (stable_n == 0u) {
		ref = lo = hi = v;
		acc = v;
		stable_n = 1u;
	} else if ((uint32_t)(v > ref ? v - ref : ref - v) <= TOL) {
		if (v < lo) { lo = v; }
		if (v > hi) { hi = v; }
		acc += v;
		stable_n++;
	} else {
		/* Wandered. Restart the run AT THIS SAMPLE rather than discarding it --
		 * a press that is still settling should not have to be released and
		 * pressed again. */
		ref = lo = hi = v;
		acc = v;
		stable_n = 1u;
	}
	return stable_n >= need;
}

static void announce(void)
{
	const struct step *s = &steps[idx];

	switch (s->kind) {
	case K_L0:
	case K_L1:
		printk("\n[%d/%d] PRESS AND HOLD:  %s\n", idx + 1, NSTEPS, s->label);
		break;
	case K_SAG_BASE:
		printk("\n[%d/%d] Set ALL FOUR faders to roughly the MIDDLE and let go.\n",
		       idx + 1, NSTEPS);
		printk("       (this is the baseline the rail sag is measured against --\n"
		       "        at the bottom of their travel a sag cannot be seen at all,\n"
		       "        which is why the first attempt produced no sag data)\n");
		break;
	case K_SAG_PRESS:
		printk("\n[%d/%d] Leave the faders where they are. PRESS AND HOLD:  %s\n",
		       idx + 1, NSTEPS, s->label);
		break;
	default:
		break;
	}
	stable_reset();
	step_ms = 0u;
	live_ms = 0u;
	dup_warned = false;
	/* Seed the stillness reference from the CURRENT readings. Left stale, the first
	 * tick of a sag step computes a huge spread against whatever the previous step
	 * ended on and throws a sample away -- self-correcting, but a re-run inherits
	 * values from the run before it. */
	for (int i = 0; i < SP1_NUM_FADERS; i++) {
		last_fad[i] = sp1_fader_raw(i);
	}
	phase = P_WAIT_PRESS;
}

static void record(uint16_t mean, uint16_t l, uint16_t h, bool ok)
{
	results[idx].mean = mean;
	results[idx].lo = l;
	results[idx].hi = h;
	results[idx].ok = ok;
}

/* ⚠️ EVERY line here calls sp1_console_pace(). CDC ACM drops characters rather than
 * blocking once its ring is full, and the 2026-09-19 run proved it: the report came
 * out shredded mid-line and a table transcribed from it would have been silently
 * wrong. Do not "tidy" the pace calls away. */
static void report(void)
{
	printk("\n\n================ LADDER CALIBRATION RESULTS ================\n");
	sp1_console_pace();
	printk("%-16s %7s %7s %7s %6s\n", "target", "mean", "min", "max", "spread");
	sp1_console_pace();

	for (int i = 0; i < NSTEPS; i++) {
		if (steps[i].kind != K_L0 && steps[i].kind != K_L1) {
			continue;
		}
		if (!results[i].ok) {
			printk("%-16s %7s\n", steps[i].label, "SKIPPED");
		} else {
			printk("%-16s %7u %7u %7u %6u%s\n", steps[i].label,
			       results[i].mean, results[i].lo, results[i].hi,
			       (unsigned)(results[i].hi - results[i].lo),
			       steps[i].kind == K_L1 ? "   (ladder 1)" : "");
		}
		sp1_console_pace();
	}

	printk("\n---- rail sag ----\n");
	sp1_console_pace();
	if (!sag_base_ok) {
		printk("no baseline captured; sag unknown\n");
	} else {
		printk("baseline  f1=%u f2=%u f3=%u f4=%u\n",
		       sag_base[0], sag_base[1], sag_base[2], sag_base[3]);
		sp1_console_pace();
		printk("(sag per press is printed above, measured idle -> loaded at the"
		       " moment of the press. This baseline only confirms the faders"
		       " were off the bottom; it is NOT what sag is measured against.)\n");
		sp1_console_pace();
	}

	printk("\n---- paste-ready ----\n");
	sp1_console_pace();
	for (int i = 0; i < NSTEPS; i++) {
		if ((steps[i].kind == K_L0 || steps[i].kind == K_L1) &&
		    results[i].ok) {
			printk("\t{ %4u, \"%s\" },%s\n", results[i].mean,
			       steps[i].label,
			       steps[i].kind == K_L1 ? "   /* ladder 1 */" : "");
			sp1_console_pace();
		}
	}
	printk("============================================================\n");
	sp1_console_pace();
	printk("Singles are means of %u stable samples; multi-button targets %u.\n",
	       NEED_SAMPLES, NEED_SAMPLES_COMBO);
	sp1_console_pace();
}

void sp1_calib_start(void)
{
	running = true;
	idx = 0;
	sag_base_ok = false;
	for (int i = 0; i < NSTEPS; i++) {
		results[i].ok = false;
	}
	printk("\n=== GUIDED LADDER CALIBRATION ===\n");
	printk("%d steps. Press what it asks for, hold until it says CAPTURED,\n", NSTEPS);
	printk("then release. A step you cannot do: just wait %u s and it skips.\n",
	       STEP_TIMEOUT_MS / 1000u);
	printk("\"••\" still powers the device off normally at any point.\n");
	phase = P_ANNOUNCE;
}

bool sp1_calib_running(void)
{
	return running;
}

static void advance(void)
{
	idx++;
	if (idx >= NSTEPS) {
		phase = P_REPORT;
	} else {
		phase = P_ANNOUNCE;
	}
}

void sp1_calib_tick(uint32_t elapsed_ms)
{
	if (!running) {
		return;
	}

	switch (phase) {
	case P_ANNOUNCE:
		announce();
		return;

	case P_REPORT:
		report();
		running = false;
		phase = P_DONE;
		return;

	case P_DONE:
		return;

	default:
		break;
	}

	step_ms += elapsed_ms;
	live_ms += elapsed_ms;

	const struct step *s = &steps[idx];

	/* ---- timeout: mark and move on, never stall the run ----
	 *
	 * ⚠️ THE TIMEOUT MUST COVER P_WAIT_RELEASE TOO. It used to be gated on
	 * `phase == P_WAIT_PRESS`, which left the release phase with no exit at all:
	 * `running` stayed true, report() never ran, and every result collected so far
	 * was lost, because they live only in results[] and are never printed until the
	 * end. The last step's release test is `rail == SP1_RAIL_IDLE`, which needs BOTH
	 * ladders quiet -- so a finger resting on VOL- or one sticky contact discarded
	 * the entire 23-step run. That directly contradicted the documented escape
	 * hatch ("wait 20 s and it skips"), which only ever covered half the machine. */
	if (step_ms >= STEP_TIMEOUT_MS) {
		if (phase == P_WAIT_PRESS) {
			printk("       ... SKIPPED (no stable press in %u s)\n",
			       STEP_TIMEOUT_MS / 1000u);
			record(0u, 0u, 0u, false);
			/* Hand to the release phase rather than straight to the next
			 * announce. A step can time out precisely BECAUSE something is
			 * held and never settles; announcing the next target with that
			 * value still on the ladder let the next step complete on it. */
			phase = P_WAIT_RELEASE;
			step_ms = 0u;
			stable_reset();
		} else {
			/* Captured fine, but the release never registered. Keep the
			 * result -- it is good data -- and move on. */
			printk("       ... moving on (release not seen in %u s;"
			       " the capture above is kept)\n",
			       STEP_TIMEOUT_MS / 1000u);
			advance();
		}
		return;
	}

	if (phase == P_WAIT_PRESS) {
		if (s->kind == K_SAG_BASE) {
			/* All four parked mid-travel, and holding still. Uses the
			 * FROZEN fader values: nothing should be pressed yet. */
			bool in_window = true;
			for (int i = 0; i < SP1_NUM_FADERS; i++) {
				const uint16_t v = sp1_fader_raw(i);
				if (v < SAG_LO || v > SAG_HI) {
					in_window = false;
				}
			}
			/* ---- ALL FOUR must be still, not just fader 0 ----
			 * The 2026-09-19 run only checked fader 0 for six samples and
			 * took the baseline while a hand was still on the panel: it
			 * recorded f4=1457 and by the time of the press f4 was 1606. A
			 * baseline 150 counts stale makes the sag figure meaningless --
			 * it came out NEGATIVE, which is how the bug was spotted. */
			uint16_t spread = 0u;
			for (int i = 0; i < SP1_NUM_FADERS; i++) {
				const uint16_t v = sp1_fader_raw(i);
				const uint16_t d = (v > last_fad[i])
						 ? (uint16_t)(v - last_fad[i])
						 : (uint16_t)(last_fad[i] - v);
				if (d > spread) { spread = d; }
				last_fad[i] = v;
			}
			if (!in_window || spread > TOL) {
				stable_reset();
			} else if (stable_feed(sp1_fader_raw(0), NEED_SAMPLES_COMBO)) {
				for (int i = 0; i < SP1_NUM_FADERS; i++) {
					sag_base[i] = sp1_fader_raw(i);
				}
				sag_base_ok = true;
				printk("       CAPTURED baseline  f1=%u f2=%u f3=%u f4=%u\n",
				       sag_base[0], sag_base[1], sag_base[2], sag_base[3]);
				record(0u, 0u, 0u, true);
				/* Nothing to release. */
				advance();
				return;
			}
		} else if (s->kind == K_SAG_PRESS) {
			/* Rail loaded, and the SCRATCH faders -- the sagged ones -- held
			 * still. sp1_fader_raw() is frozen during a press by design, so
			 * it would show no sag at all; that is the whole point of the
			 * separate accessor. */
			/* No baseline means this step is UNSATISFIABLE -- say so and skip
			 * immediately rather than making the operator hold a button for
			 * 20 s while the live line shows plausible-looking numbers. */
			if (!sag_base_ok) {
				printk("       SKIPPED: no fader baseline was captured,"
				       " so a sag cannot be measured.\n");
				record(0u, 0u, 0u, false);
				advance();
				return;
			}

			/* ALL FOUR must be holding still, not just fader 0 -- the same
			 * defect that was fixed for the baseline step and left here. If
			 * f2..f4 are still moving when the press is captured, their
			 * deltas get compared against a baseline they no longer
			 * correspond to, which is how the first run produced a negative
			 * "sag". */
			uint16_t spread = 0u;
			for (int i = 0; i < SP1_NUM_FADERS; i++) {
				const uint16_t v = sp1_fader_scratch(i);
				const uint16_t d = (v > last_fad[i])
						 ? (uint16_t)(v - last_fad[i])
						 : (uint16_t)(last_fad[i] - v);
				if (d > spread) { spread = d; }
				last_fad[i] = v;
			}

			if (sp1_rail_state() != SP1_RAIL_LOADED || spread > TOL) {
				stable_reset();
			} else if (stable_feed(sp1_fader_scratch(0), NEED_SAMPLES)) {
				/* SIGNED deltas, all four reported. The old version took
				 * an unsigned "drop", so every reading ABOVE the baseline
				 * clamped to zero and `d >= worst_drop` then latched onto
				 * the last index -- printing "worst F4, sag 0 counts" for
				 * a reading that had gone UP by 148. A sign error that
				 * reports exactly 0.0%% is indistinguishable from the
				 * genuine answer, which is the worst kind. */
				/* ---- measure against the LAST IDLE SAMPLE, not the baseline ----
				 *
				 * The 2026-09-20 run measured against the step-23 baseline
				 * and reported F3 -239 and F4 -163 -- readings that ROSE
				 * under load. They had not: the waiting lines of the very
				 * next step, rail=i with nothing pressed, already read F3
				 * 1741 and F4 1710. The faders moved between the steps, and
				 * the stillness check could not catch it because it compares
				 * consecutive 8 ms samples -- a slow drag of a few hundred
				 * counts over a couple of seconds passes it every time.
				 *
				 * Any baseline taken in a separate step, with a human moving
				 * in between, has this problem. The fix needs no new state:
				 * sp1_fader_raw() freezes the instant the rail loads, so
				 * right now it IS the last idle sample -- at most one 8 ms
				 * scan before the press, far too short for a hand to move a
				 * fader. Idle vs loaded, same positions. That is the
				 * measurement, and it gave clean numbers from the same log:
				 * T1 ~1 count, PLAY ~33, all four faders agreeing. */
				printk("       CAPTURED with %s held"
				       " (idle -> loaded, positive = sag):\n", s->label);
				for (int i = 0; i < SP1_NUM_FADERS; i++) {
					const uint16_t idle = sp1_fader_raw(i);
					const uint16_t load = sp1_fader_scratch(i);
					const int16_t d = (int16_t)((int32_t)idle -
								    (int32_t)load);
					printk("         F%d  %4u -> %4u   sag %+d\n",
					       i + 1, idle, load, d);
					sp1_console_pace();
				}
				record(0u, 0u, 0u, true);
				phase = P_WAIT_RELEASE;
				stable_reset();
				return;
			}
		} else {
			const uint16_t v = ladder_for(idx);
			const bool combo = is_combo(s->label);
			const uint32_t need = combo ? NEED_SAMPLES_COMBO : NEED_SAMPLES;

			if (v < PRESS_MIN) {
				stable_reset();
			} else if (stable_feed(v, need)) {
				const uint16_t mean = (uint16_t)(acc / stable_n);

				/* A value we have already recorded under a different
				 * name means the press is incomplete -- one of the
				 * buttons is not down yet. Say which, and keep
				 * waiting rather than recording a wrong number that
				 * looks right. */
				const char *dup = duplicate_of(mean);
				if (dup != NULL) {
					/* Rate-limited: the plateau re-completes every
					 * settle window while the operator keeps holding
					 * the partial press, which over a 20 s step is a
					 * lot of unpaced lines into a ring buffer that
					 * drops rather than blocks. */
					if (!dup_warned) {
						dup_warned = true;
						printk("       ...that is %s, not %s --"
						       " keep holding and add the rest\n",
						       dup, s->label);
					}
					stable_reset();
					return;
				}

				printk("       CAPTURED %-12s mean=%u  (min %u, max %u,"
				       " spread %u, %u samples)   release now\n",
				       s->label, mean, lo, hi, (unsigned)(hi - lo),
				       stable_n);
				record(mean, lo, hi, true);
				phase = P_WAIT_RELEASE;
				stable_reset();
				return;
			}
		}

		/* Still waiting. Say what we can see, so a step that never triggers
		 * still leaves the raw evidence in the log. */
		if (live_ms >= LIVE_PERIOD_MS) {
			live_ms = 0u;
			if (s->kind == K_SAG_BASE) {
				printk("       ...waiting  f1=%4u f2=%4u f3=%4u f4=%4u"
				       "  (want %u-%u on all four)\n",
				       sp1_fader_raw(0), sp1_fader_raw(1),
				       sp1_fader_raw(2), sp1_fader_raw(3),
				       SAG_LO, SAG_HI);
			} else if (s->kind == K_SAG_PRESS) {
				printk("       ...waiting  rail=%s f1=%4u f2=%4u f3=%4u"
				       " f4=%4u\n",
				       sp1_rail_state() == SP1_RAIL_LOADED ? "L" : "i",
				       sp1_fader_scratch(0), sp1_fader_scratch(1),
				       sp1_fader_scratch(2), sp1_fader_scratch(3));
			} else {
				/* Denominator must be the one THIS step uses, or a combo
				 * step reports nonsense like "stable 9/6". */
				printk("       ...waiting  lad%d=%4u  (stable %u/%u)\n",
				       s->kind == K_L1 ? 1 : 0, ladder_for(idx),
				       stable_n,
				       is_combo(s->label) ? NEED_SAMPLES_COMBO
							  : NEED_SAMPLES);
			}
		}
		return;
	}

	/* P_WAIT_RELEASE */
	{
		const uint16_t v = (s->kind == K_SAG_PRESS)
				 ? sp1_ladder_raw(0) : ladder_for(idx);
		const bool idle = (s->kind == K_SAG_PRESS)
				? (sp1_rail_state() == SP1_RAIL_IDLE)
				: (v <= IDLE_MAX);
		if (idle) {
			if (++idle_n >= NEED_IDLE_SAMPLES) {
				advance();
			}
		} else {
			idle_n = 0u;
			if (live_ms >= LIVE_PERIOD_MS) {
				live_ms = 0u;
				printk("       ...release it  (lad=%u)\n", v);
			}
		}
	}
}
