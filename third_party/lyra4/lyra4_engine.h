// LYRA-4 four-voice engine. Original C++ implementation of the signal graph traced from
// MikeMorenoDSP/LIRA-8 `LIRA-4_MobMuPlat/_LIRA-4.pd` (BSD-3-Clause, see NOTICE).
// No heap allocation, no exceptions, mono float output.
#pragma once
#include <cstdint>
#include "lyra4_config.h"
#include "lyra4_blocks.h"

namespace lyra4 {

// Parameter ids mirror the Pd GUI addresses (all 0..1 unless noted) so a Pd-driven test can be
// replayed against the engine one-to-one.
enum Param : int {
  kTune1, kTune2, kTune3, kTune4, kPitch,
  kMod12, kMod34, kSharp12, kSharp34,
  kSource12, kSource34,           // 0,1,2 (2 = other pair, 0 = LFO / total feedback, 1 = off)
  kTotalFb,                       // 0/1
  kHold, kVibrato,                // hold 0..1, vibrato 0/1
  kFast12, kFast34,               // 0/1
  kSensor1, kSensor2, kSensor3, kSensor4,  // 0/1 gates
  kVolume, kDistDrive, kDistMix,
  kDelayWaveform, kDelaySource,   // waveform 0/1, source 0,1,2
  kDelayMod, kDelayFb, kDelayMix, kDelayTime,
  kLfoFreqA, kLfoFreqB, kLfoLink, kLfoAndOr,
  kParamCount
};

struct Options {
  bool quantizeTuning = false;   // true: semitone-quantized like the Pd reference
  bool dcBlock = true;           // final 2 Hz DC blocker (the reference leaves ~0.01 DC)
};

class Engine {
 public:
  // jumpToDefaults=false reproduces Pd's load state (every ramp starts from 0), for A/B tests.
  void init(float sampleRate, bool jumpToDefaults = true);
  void reset();
  void setParam(int id, float v);
  float param(int id) const { return p_[id]; }
  void setOptions(const Options& o) { opt_ = o; }
  // Latency in samples of each pair-to-pair send (pairFrom: delay seen by pair 0 reading pair 1, and
  // vice versa; 0 = same sample) and of the total-feedback send (>= 1).
  void setCrossDelays(int d0from1, int d1from0, int tfb);
  void process(float* out, int n);         // mono
  uint32_t recoveries() const { return recoveries_; }

 private:
  struct Line {
    float cur = 0, tgt = 0, step = 0; int n = 0;
    void to(float t, float ms, float fs);
    void set(float v) { cur = tgt = v; n = 0; step = 0; }
    inline float tick() { if (n > 0) { cur += step; if (--n == 0) cur = tgt; } return cur; }
    // Advance k samples at once (control-rate update); equals k calls to tick().
    inline float advance(int k) { if (n > 0) { if (k >= n) { cur = tgt; n = 0; } else { cur += step * (float)k; n -= k; } } return cur; }
  };
  struct OnePole { float y = 0; };
  struct Osc {
    float ph = 0, pw = 0.53125f, lp = 0, aCoef = 0, lastFc = -1.0f;
    float outSq = 0, outTri = 0;
    void run(float freq, float skewIn, float fs, float invFs);
  };
  struct Pair {
    Osc osc[2];
    Line env[2];
    float attackMs = 200, releaseMs = 8000;
    float hpX = 0, hpY = 0;      // hip~ 3 for the send
    float vibPh = 0, vibRate = 1;
    Line vibAmt, sharp, mod;
    float sensorState[2] = {0, 0};
  };

  // Values that only change at control rate (every kCtl samples): ramps, gates, reciprocals.
  static constexpr int kCtl = 16;
  struct Ctl {
    float f[4];                  // oscillator base frequencies (tune * pitch), Hz
    float depth[2], sharp[2];    // cross-FM depth, waveform mix
    float gv[2], g0[2], gt[2], gl[2];   // modulation-source gates per pair
    float fMul[2], skew[2];      // vibrato: frequency multiplier and pulse-width offset
    float gate[4], bump[4];      // per-oscillator gate and envelope "bump"
    float tms, dfb, invFbNorm, dmx, dmod;   // delay
    float drive, invGclip, dmix, vol;       // master
    float lfoFa, lfoFb;
    bool wave1; int dsrc; bool andOr; float link;
  } c_;
  void controlStep(int k);
  void renderChunk(float* out, int k);
  void applyParam(int id);
  void updateTuning(int pair);
  float lfoStep(float& sqBipolar, float& tri, float& sqUni);

  float fs_ = 48000;
  float p_[kParamCount] = {};
  Options opt_;
  Pair pair_[2];
  Line tuneLine_[4], pitch_, hold_, vol_, drive_, dmix_;
  Line dTime_, dFb_, dMix_, dMod_;
  Line lfoFa_, lfoFb_;
  float lfoPhA_ = 0, lfoPhB_ = 0;
  // delay
  static constexpr int kCap = LYRA4_DELAY_CAPACITY;
#if LYRA4_DELAY_INT16
  int16_t dbuf_[kCap];
#else
  float dbuf_[kCap];
#endif
  int dw_ = 0;
  float dHp_x = 0, dHp_y = 0, dLp_ = 0, selfLp_ = 0;
  Compressor comp_;
  Expander expander_;
  // master
  float mHp20_x = 0, mHp20_y = 0, mHp10_x = 0, mHp10_y = 0;
  float tfbHp_x = 0, tfbHp_y = 0, dcX_ = 0, dcY_ = 0;
  // sends (ring, so a cross delay > 1 sample is possible)
  static constexpr int kRing = 128;
  float sendRing_[3][kRing] = {};  // 0: voice12, 1: voice34, 2: total feedback
  int ringPos_ = 0;
  int crossD_[2] = {LYRA4_CROSS_D0, LYRA4_CROSS_D1};
  int tfbD_ = LYRA4_CROSS_DTFB;
  float curSend_[2] = {0, 0};
  uint32_t rng_ = 22222u;
  uint32_t recoveries_ = 0;
  float tMinMs_ = 1.45125f, tMaxMs_ = LYRA4_DELAY_MAX_MS;
};

// Runs the engine at half the output rate and upsamples (the CPU-saving mode for the SP-1: init with
// the output rate; the engine itself runs at outputRate/2). Output block length must be even.
class HalfRateEngine {
 public:
  void init(float outputRate) { e_.init(outputRate * 0.5f); up_.reset(); }
  Engine& engine() { return e_; }
  void process(float* out, int n) {
    float tmp[kMaxChunk];
    int done = 0;
    while (done < n) {
      const int m = (n - done) / 2 < kMaxChunk ? (n - done) / 2 : kMaxChunk;
      e_.process(tmp, m);
      for (int i = 0; i < m; ++i) up_.push(tmp[i], out[done + 2 * i], out[done + 2 * i + 1]);
      done += 2 * m;
    }
  }
 private:
  static constexpr int kMaxChunk = 48;
  Engine e_;
  Upsampler2x up_;
};

}  // namespace lyra4
