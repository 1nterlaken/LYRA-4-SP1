/*
 * LYRA-4 voice for the SP-1: a C interface around the lyra4 engine (third_party/lyra4),
 * the same shape as sp1_synth.h so sp1_audio.c's render path has one seam.
 *
 *   sp1_lyra_init()      main thread, once, before audio starts (builds tables, 36 KB static)
 *   sp1_lyra_reset()     main thread, only while the audio thread is parked (entry to ON)
 *   sp1_lyra_render()    AUDIO THREAD ONLY
 *   sp1_lyra_publish()   main thread: lock-free double buffer, read once per block
 *
 * Engine at 24 kHz + 2x upsampler (HalfRateEngine), 600 ms int16 delay: decisions of
 * LYRA4_INTEGRATION_PLAN.md section 6.
 */
#ifndef SP1_LYRA_H
#define SP1_LYRA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sp1_lyra_init(void);
void sp1_lyra_reset(void);
void sp1_lyra_publish(const float *params, bool quantize);   /* lyra_ctl.h's vector */
void sp1_lyra_render(int16_t *out, uint32_t frames);         /* frames: multiple of 2, <= 96 */

#ifdef __cplusplus
}
#endif
#endif
