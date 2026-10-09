/*
 * LYRA-4 — STANDBY: off, but plugged in.
 *
 * Why it exists, beyond being a nice charge display: you cannot SYSTEM_OFF while
 * USB is plugged. With VBUS already high there is no wake EDGE, so the device
 * would go dark and stay dark until the cable was pulled -- indistinguishable
 * from a brick. M0/M1a papered over that with a soft reset. Standby is where
 * that transition should actually land, and being already awake it needs no
 * reset at all.
 *
 * Display:
 *   play row (the four LEDs between "••" and PLAY) = charge bar, filling from
 *       the "••" end toward PLAY. Breathes while charging, solid when complete.
 *   model row T2 = USB power present, T3 = charging. Both dim (~30 %), so they
 *       read as status lights rather than as UI.
 *
 * Transitions out:
 *   "••" held SP1_PWR_ON_HOLD_MS -> ON
 *   USB unplugged                -> SYSTEM_OFF (possible again: no VBUS)
 */
#ifndef SP1_STANDBY_H
#define SP1_STANDBY_H

#include <stdbool.h>

/* Enter standby and stay until the user powers on or unplugs. Returns when the
 * device should transition to ON; does not return if it powers off.
 *
 * Feeds the watchdog throughout -- standby is a RUNNING state, not sleep. */
void sp1_standby_run(void);

#endif /* SP1_STANDBY_H */
