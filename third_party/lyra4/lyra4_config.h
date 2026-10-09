// LYRA-4 engine compile-time configuration. Everything here is a build-time decision so the
// same source builds on the desktop and (later) on the SP-1 without runtime allocation.
#pragma once

// Build profile for the SP-1: the engine runs at 24 kHz (HalfRateEngine) and is upsampled to the 48 kHz
// codec rate. At 24 kHz the same 28.8 KB of int16 memory holds 600 ms of delay.
#ifdef LYRA4_PROFILE_SP1_HALF
#define LYRA4_MAX_FS 24000
#define LYRA4_DELAY_MAX_MS 600
#endif

// Longest delay time in milliseconds. The LIRA-4 reference is 5944 ms; that is far too
// large for the nRF52840 (256 KB RAM, shared with the LYRA-4 image). Start at 300 ms and
// settle the final value from listening tests and the LYRA-4 RAM map.
#ifndef LYRA4_DELAY_MAX_MS
#define LYRA4_DELAY_MAX_MS 300
#endif

// Highest sample rate the static delay buffer must cover (SP-1 path is 48 kHz).
#ifndef LYRA4_MAX_FS
#define LYRA4_MAX_FS 48000
#endif

// 1: delay memory is int16 (SP-1 target, halves RAM). 0: float (desktop comparison only).
#ifndef LYRA4_DELAY_INT16
#define LYRA4_DELAY_INT16 1
#endif

// Full-scale of the int16 delay storage, in signal units. 2.0 keeps headroom for the
// feedback path (the written signal can exceed 1.0) at ~14 usable bits.
#ifndef LYRA4_DELAY_HEADROOM
#define LYRA4_DELAY_HEADROOM 2.0f
#endif

// 1: 4-point Lagrange read (what Pd's vd~ does). 0: linear (cheaper).
#ifndef LYRA4_DELAY_LAGRANGE
#define LYRA4_DELAY_LAGRANGE 1
#endif

// Latency (samples) of the sends, traced from how Pd orders the patch's s~/r~ pairs and confirmed
// by A/B against libpd (tools/loop_latency_probe.py): pair 1-2 reads pair 3-4 one 64-sample block
// late, pair 3-4 reads pair 1-2 in the same sample, total feedback is one block late.
// Runtime-settable with Engine::setCrossDelays.
#ifndef LYRA4_CROSS_D0
#define LYRA4_CROSS_D0 64
#endif
#ifndef LYRA4_CROSS_D1
#define LYRA4_CROSS_D1 0
#endif
#ifndef LYRA4_CROSS_DTFB
#define LYRA4_CROSS_DTFB 64
#endif

#define LYRA4_DELAY_CAPACITY ((int)(LYRA4_DELAY_MAX_MS * 0.001 * LYRA4_MAX_FS) + 16)
