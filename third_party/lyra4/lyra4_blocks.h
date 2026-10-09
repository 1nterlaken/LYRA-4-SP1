// Small DSP blocks shared by the engine and the probe tools. Header-only, no allocation.
#pragma once
#include <cmath>
#include <algorithm>

namespace lyra4 {

inline float clampf_(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush_(float x) { return std::fabs(x) < 1e-25f ? 0.0f : x; }
// 2^x for x <= 0 without libm (degree-4 polynomial, ~4e-5 relative error), the cost class of ~25 cycles
// on a Cortex-M4F instead of a ~300-cycle expf/powf call.
inline float fastExp2Neg(float x) {
  if (x < -24.0f) return 0.0f;
  if (x > 0.0f) x = 0.0f;
  const int xi = (int)x - 1;                 // floor for x <= 0 (and one below for exact integers)
  const float f = x - (float)xi;             // f in [0,2)
  const float fr = f - 1.0f;                 // 2^x = 2^(xi+1) * 2^(f-1), f-1 in [0,1)
  const float p = 1.0f + fr * (0.69314718f + fr * (0.24022651f + fr * (0.05550411f + fr * 0.00961813f)));
  union { float f; unsigned u; } s; s.u = (unsigned)(xi + 1 + 127) << 23;
  return p * s.f;
}
// sin(2*pi*t) for t in [0,1) from a 513-entry table with linear interpolation (|error| < 2e-5).
// The table is a plain global filled once by sinTableInit() (Engine::init calls it): no function-local
// statics, so no thread-safe-static guard or unwinder is linked into firmware.
inline float g_sinTable[513];
inline bool g_sinTableReady = false;
inline void sinTableInit() {
  if (g_sinTableReady) return;
  for (int i = 0; i <= 512; ++i) g_sinTable[i] = std::sin(6.28318530717959f * (float)i / 512.0f);
  g_sinTableReady = true;
}
inline float sinTurns(float turns) {
  float x = turns - (float)(int)turns; if (x < 0.0f) x += 1.0f;
  const float p = x * 512.0f; const int i = (int)p; const float fr = p - (float)i;
  return g_sinTable[i] + fr * (g_sinTable[i + 1] - g_sinTable[i]);
}
inline float padeTanh_(float x) {
  x = clampf_(x, -3.0f, 3.0f);
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// compressor~ : peak envelope follower -> hard-knee gain computer -> Pade tanh of the gain.
struct Compressor {
  // The patch picks attack or release by comparing |x| with its own envelope received through
  // r~ from a send~ that sits *downstream*, so Pd delivers it one 64-sample block late. Emulated
  // with a 64-sample history; set kPdBlockLatency=0 for a same-sample comparison.
  static constexpr int kPdBlockLatency = 64;
  float env = 0, cAtk = 0, cRel = 0, thrLin = 0.25f, invR = 0.2f;
  float hist[kPdBlockLatency > 0 ? kPdBlockLatency : 1] = {};
  int hpos = 0;
  void set(float fs, float atkMs, float relMs, float thrDb, float ratio) {
    // Patch: exp(-4.60517 / max(1, ms*fs/1000)), i.e. the envelope settles to 1% in the stated time.
    cAtk = std::exp(-4.60517f / std::max(1.0f, fs * atkMs * 0.001f));
    cRel = std::exp(-4.60517f / std::max(1.0f, fs * relMs * 0.001f));
    thrLin = std::pow(10.0f, thrDb / 20.0f);
    invR = 1.0f / std::max(1.0f, ratio);
  }
  void reset() { env = 0; for (float& v : hist) v = 0; hpos = 0; }
  inline float run(float x) {
    const float ax = std::fabs(x);
    float ref = env;
    if (kPdBlockLatency > 0) { ref = hist[hpos]; hist[hpos] = env; hpos = (hpos + 1) % kPdBlockLatency; }
    const float c = ax > ref ? cAtk : cRel;
    env = flush_(c * env + (1.0f - c) * ax);
    float g = ((env - thrLin) * invR + thrLin) / (env + 1e-20f);
    g = padeTanh_(clampf_(g, 0.0f, 1.0f));
    return x * g;
  }
};

// expander~ : Hann-weighted 512-sample mean power every 256 samples (Pd env~ 512), downward
// expansion below threshold, gain smoothed with an 11 ms line~.
struct Expander {
  float ring[512] = {}, hann[512] = {};
  int pos = 0, cnt = 0, rampN = 0, rampLen = 1;
  float gDb = 0, gTarget = 0, gStep = 0, lin = 1, thrDb = -60, slope = 0.8f;
  bool inited = false;
  void set(float fs, float thr, float ratio) {
    thrDb = thr; slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    rampLen = std::max(1, (int)(11.0f * 0.001f * fs));
    if (!inited) { for (int i = 0; i < 512; ++i) hann[i] = (1.0f - std::cos(6.28318530717959f * i / 512.0f)) / 512.0f; inited = true; }
  }
  void reset() { for (float& v : ring) v = 0; pos = cnt = rampN = 0; gDb = gTarget = 0; lin = 1; }
  inline float run(float x) {
    ring[pos] = x; pos = (pos + 1) & 511;
    if (++cnt >= 256) {
      cnt = 0;
      float p = 0;
      for (int i = 0; i < 512; ++i) { const float v = ring[(pos + i) & 511]; p += hann[i] * v * v; }
      const float dbfs = p > 1e-10f ? 10.0f * std::log10(p) : -100.0f;
      gTarget = std::min(0.0f, (dbfs - thrDb) * slope);
      rampN = rampLen; gStep = (gTarget - gDb) / (float)rampLen;
    }
    if (rampN > 0) {
      gDb += gStep; if (--rampN == 0) gDb = gTarget;
      lin = (gDb + 100.0f) <= 0.0f ? 0.0f : fastExp2Neg(gDb * 0.16609640474f);
    }
    return x * lin;
  }
};

// 2x half-band upsampler for running the engine at half rate (24 kHz -> 48 kHz).
// 31-tap Kaiser(8) half-band: even outputs are the delayed input, odd outputs are 8 symmetric pairs.
// Latency 8 input samples (0.33 ms at 24 kHz). Passband ripple 0.11 dB below 9 kHz.
struct Upsampler2x {
  float buf[32] = {};      // each sample is stored twice (pos and pos+16) so the 16-sample window is contiguous
  int pos = 0;
  void reset() { for (float& v : buf) v = 0; pos = 0; }
  inline void push(float x, float& even, float& odd) {
    static constexpr float kC[8] = {0.62609105f, -0.18244962f, 0.08307341f, -0.03845528f,
                                    0.01604065f, -0.00546888f, 0.00128451f, -9.926e-05f};
    buf[pos] = x; buf[pos + 16] = x; pos = (pos + 1) & 15;
    const float* a = buf + pos;                    // a[0] oldest .. a[15] newest
    even = a[7];
    odd = kC[0] * (a[7] + a[8]) + kC[1] * (a[6] + a[9]) + kC[2] * (a[5] + a[10]) + kC[3] * (a[4] + a[11]) +
          kC[4] * (a[3] + a[12]) + kC[5] * (a[2] + a[13]) + kC[6] * (a[1] + a[14]) + kC[7] * (a[0] + a[15]);
  }
};

}  // namespace lyra4
