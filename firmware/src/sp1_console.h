/*
 * LYRA-4 — USB CDC ACM console.
 *
 * Exists mainly to make two things possible that are miserable blind:
 *   1. calibrating the battery divider (see sp1_batt.h)
 *   2. building the button-ladder threshold tables in M1d, which means watching
 *      raw ADC values while pressing buttons and combinations
 *
 * ⚠️ NEVER call into this from the audio path once M2 lands. printk over CDC ACM
 * can block for milliseconds when a host is attached and the endpoint is busy.
 * The audio budget is 1333 cycles per sample. Status printing belongs in the
 * ~8 ms control loop and nowhere else.
 *
 * ⚠️ It must NOT wait for a host. Blocking on DTR would mean the device does not
 * boot unplugged, which is most of the time.
 */
#ifndef SP1_CONSOLE_H
#define SP1_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/* Bring up USB and the CDC ACM console. Returns 0 on success. Never blocks
 * waiting for a host; if USB fails the rest of the firmware carries on silently. */
int sp1_console_init(void);

/* Record and print the boot banner: build identity, reset reason, and the fault
 * breadcrumb from the previous boot if there was one. Call once, after init.
 *
 * The values are RETAINED, and re-printed by sp1_console_poll() every time a
 * terminal attaches (DTR rising). Printing once at boot loses it: USB enumeration
 * takes hundreds of milliseconds and the host may not open the port for seconds
 * after that. Confirmed on hardware 2026-09-20 -- a capture showed nine buffered
 * status lines flushed on attach but no banner. Waiting for a host is not an
 * option; the device has to boot unplugged. */
void sp1_console_banner(uint32_t resetreas, bool had_fault,
			uint32_t fault_reason, uint32_t fault_pc);

/* Set how often status lines are emitted, in milliseconds.
 * SP1_CONSOLE_PERIOD_OFF (0) disables them; the banner and the calibration line
 * still appear. Each loop sets this on entry, because ON and STANDBY want very
 * different cadences -- see sp1_ui_timing.h.
 *
 * ⚠️ M2: set this to OFF once audio is live unless actively debugging. printk over
 * CDC ACM can block for an unbounded time if the host stalls, which is a dropout
 * rather than a percentage of CPU. */
void sp1_console_set_status_period(uint32_t ms);

/* Call between the lines of any multi-line dump. CDC ACM drops characters rather
 * than blocking once its ring buffer is full, so a forty-line burst inside one
 * control tick loses data mid-line -- confirmed on hardware, see sp1_console.c.
 * Control loop only; never from the audio path. */
void sp1_console_pace(void);

/* Raw control capture (M1d-a). When on, every channel is dumped at ~20 Hz so the
 * ladder threshold tables can be MEASURED rather than guessed. Off by default;
 * this is a deliberate firehose and it must not be left on with audio running. */
void sp1_console_set_raw_capture(bool on);
bool sp1_console_raw_capture(void);

/* Call once per control tick from every loop that runs (ON and STANDBY). Emits a
 * status line about once a second, and the calibration line whenever the charger
 * reports complete. Cheap when there is nothing to say. */
void sp1_console_poll(uint32_t elapsed_ms, const char *state);

#endif /* SP1_CONSOLE_H */
