// LYRA-4 engine implementation. See LYRA4_DSP_SPEC.md for the graph this follows.
#include "lyra4_engine.h"
#include "lyra4_blocks.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace lyra4 {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kRampMs = 23.22f;  // Pd's `$1 23.22` line~ messages used on nearly every knob

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float flush(float x) { return std::fabs(x) < 1e-25f ? 0.0f : x; }

// Pd mtof.
inline float mtof(float m) { return m <= -1500.0f ? 0.0f : 8.17579891564f * std::exp2(m * (1.0f / 12.0f)); }

// The patch's `tanh~` subpatch: clip to +-3, then the Pade approximant x(27+x^2)/(27+9x^2).
inline float padeTanh(float x) {
  x = clampf(x, -3.0f, 3.0f);
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Pd hip~ (one-pole): new = x + coef*last; out = new - last; last = new.
struct Hip {
  float last = 0;
  float coef = 0.99f;
  void setCutoff(float fc, float fs) { coef = clampf(1.0f - fc * kTwoPi / fs, 0.0f, 1.0f); }
  inline float run(float x) { float n = x + coef * last; float o = n - last; last = flush(n); return o; }
};
// Pd lop~: y += coef*(x - y).
struct Lop {
  float y = 0;
  float coef = 0.5f;
  void setCutoff(float fc, float fs) { coef = clampf(fc * kTwoPi / fs, 0.0f, 1.0f); }
  inline float run(float x) { y += coef * (x - y); y = flush(y); return y; }
};

// PolyBLEP residual as the patch computes it, negated to the usual sign convention:
// returns the standard residual; the saw is (2p-1) - residual.
inline float blep(float t, float dt) {
  if (t < dt) { const float v = t / dt; return 2.0f * v - v * v - 1.0f; }
  if (t > 1.0f - dt) { const float u = (t - 1.0f) / dt; return u * u + 2.0f * u + 1.0f; }
  return 0.0f;
}

}  // namespace

void Engine::Line::to(float t, float ms, float fs) {
  const int k = static_cast<int>(ms * 0.001f * fs + 0.5f);
  tgt = t;
  if (k <= 0) { cur = t; n = 0; step = 0; }
  else { n = k; step = (t - cur) / static_cast<float>(k); }
}

// os.triangle~ : band-limited pulse (difference of two PolyBLEP saws, duty latched per cycle),
// then a one-pole low-pass of the pulse for the "triangle" output.
void Engine::Osc::run(float freq, float skewIn, float fs, float invFs) {
  const float f = std::fabs(freq);
  const float dt = std::min(f * invFs, 0.49f);
  ph += dt;
  if (ph >= 1.0f) { ph -= 1.0f; if (ph >= 1.0f) ph = 0.0f; pw = clampf(skewIn, 0.0f, 1.0f); }
  float p2 = ph + pw; if (p2 >= 1.0f) p2 -= 1.0f;
  const float saw1 = 2.0f * ph - 1.0f - blep(ph, dt);
  const float saw2 = 2.0f * p2 - 1.0f - blep(p2, dt);
  const float pulse = (saw1 - saw2) + 2.0f * pw - 1.0f;
  outSq = pulse * 0.59f;
  const float fc = std::min(std::max(f * 0.25f, 100.0f), fs * 0.5f);
  if (fc != lastFc) {                            // exp(-2*pi*fc/fs); the reference hard-codes 44100 here, we use real fs
    aCoef = fastExp2Neg(-(kTwoPi * 1.44269504f) * fc * invFs);
    lastFc = fc;
  }
  lp = flush((1.0f - aCoef) * pulse + aCoef * lp);
  outTri = lp * 2.65f - 0.145f;
}

void Engine::init(float sampleRate, bool jumpToDefaults) {
  fs_ = sampleRate;
  sinTableInit();
  comp_.set(fs_, 2.5f, 2.5f, -12.0f, 5.0f);   // traced: attack, release, threshold dB, ratio
  expander_.set(fs_, -60.0f, 5.0f);          // traced: threshold dB, ratio
  const float maxMsHere = (kCap - 8) / fs_ * 1000.0f;
  tMaxMs_ = std::min(static_cast<float>(LYRA4_DELAY_MAX_MS), maxMsHere);
  tMinMs_ = 64.0f / fs_ * 1000.0f;   // Pd vd~ cannot read closer than one 64-sample block
  reset();
  static const float defaults[kParamCount] = {
      0.4f, 0.6f, 0.5f, 0.5f, 0.5f,  // tune1..4, pitch
      0, 0, 0.5f, 0.5f,              // mod12, mod34, sharp12, sharp34
      2, 2, 0,                       // source12, source34, totalFb
      0, 0, 0, 0,                    // hold, vibrato, fast12, fast34
      0, 0, 0, 0,                    // sensors
      1, 0.5f, 0.5f,                 // volume, drive, dist mix
      0, 2, 0.1f, 0.5f, 0.5f, 0.5f,  // delay waveform, source, mod, fb, mix, time
      0.6f, 0.7f, 0, 0};             // lfo A, B, link, andOr
  for (int i = 0; i < kParamCount; ++i) { p_[i] = defaults[i]; applyParam(i); }
  if (!jumpToDefaults) return;
  // Jump all ramps to their targets so the first sample is at the defaults.
  for (auto& l : tuneLine_) l.cur = l.tgt;
  pitch_.cur = pitch_.tgt; hold_.cur = hold_.tgt; vol_.cur = vol_.tgt; drive_.cur = drive_.tgt;
  dmix_.cur = dmix_.tgt; dTime_.cur = dTime_.tgt; dFb_.cur = dFb_.tgt; dMix_.cur = dMix_.tgt;
  dMod_.cur = dMod_.tgt; lfoFa_.cur = lfoFa_.tgt; lfoFb_.cur = lfoFb_.tgt;
  for (auto& pr : pair_) {
    pr.sharp.cur = pr.sharp.tgt; pr.mod.cur = pr.mod.tgt; pr.vibAmt.cur = pr.vibAmt.tgt;
    pr.sharp.n = pr.mod.n = pr.vibAmt.n = 0;
  }
  for (auto* l : {&pitch_, &hold_, &vol_, &drive_, &dmix_, &dTime_, &dFb_, &dMix_, &dMod_, &lfoFa_, &lfoFb_}) l->n = 0;
  for (auto& l : tuneLine_) l.n = 0;
}

void Engine::reset() {
  std::memset(dbuf_, 0, sizeof(dbuf_));
  std::memset(sendRing_, 0, sizeof(sendRing_));
  comp_.reset(); expander_.reset();
  dw_ = 0; ringPos_ = 0;
  dHp_x = dHp_y = dLp_ = selfLp_ = 0;
  mHp20_x = mHp20_y = mHp10_x = mHp10_y = tfbHp_x = tfbHp_y = dcX_ = dcY_ = 0;
  lfoPhA_ = lfoPhB_ = 0;
  pair_[0].vibRate = 1.37f; pair_[1].vibRate = 2.11f;   // reference draws 0.5..3.5 Hz at load
  for (auto& pr : pair_) {
    pr.osc[0] = Osc(); pr.osc[1] = Osc();
    pr.env[0] = Line(); pr.env[1] = Line();
    pr.hpX = pr.hpY = 0; pr.vibPh = 0;
  }
}

void Engine::setCrossDelays(int a, int b, int tfb) {
  crossD_[0] = std::max(0, std::min(a, kRing - 1));
  crossD_[1] = std::max(0, std::min(b, kRing - 1));
  tfbD_ = std::max(1, std::min(tfb, kRing - 1));
}

void Engine::setParam(int id, float v) {
  if (id < 0 || id >= kParamCount) return;
  p_[id] = v;
  applyParam(id);
}

void Engine::updateTuning(int i) {
  // Per-voice ranges traced from the patch: tune1/2 = int(v*93)-16, tune3 = int(v*109)+7,
  // tune4 = int(v*107)+9 (MIDI note numbers; the reference truncates to integers).
  static const float kSpan[4] = {93.0f, 93.0f, 109.0f, 107.0f};
  static const float kOffs[4] = {-16.0f, -16.0f, 7.0f, 9.0f};
  const float raw = p_[kTune1 + i] * kSpan[i];
  const float midi = (opt_.quantizeTuning ? std::floor(raw) : raw) + kOffs[i];
  tuneLine_[i].to(mtof(midi), 100.0f, fs_);
}

void Engine::applyParam(int id) {
  const float v = p_[id];
  switch (id) {
    case kTune1: case kTune2: case kTune3: case kTune4: updateTuning(id - kTune1); break;
    case kPitch: pitch_.to(v * 1.99f + 0.01f, kRampMs, fs_); break;
    case kMod12: pair_[0].mod.to(std::pow(v, 4.0f) * 2.0f, kRampMs, fs_); break;
    case kMod34: pair_[1].mod.to(std::pow(v, 4.0f) * 2.0f, kRampMs, fs_); break;
    case kSharp12: pair_[0].sharp.to(v * v, kRampMs, fs_); break;
    case kSharp34: pair_[1].sharp.to(v * v, kRampMs, fs_); break;
    case kHold: hold_.to(v * v, kRampMs, fs_); break;
    case kVibrato: pair_[0].vibAmt.to(v > 0.5f ? 1.0f : 0.0f, kRampMs, fs_);
                   pair_[1].vibAmt.to(v > 0.5f ? 1.0f : 0.0f, kRampMs, fs_); break;
    case kFast12: pair_[0].attackMs = v > 0.5f ? 100.0f : 200.0f; pair_[0].releaseMs = v > 0.5f ? 100.0f : 8000.0f; break;
    case kFast34: pair_[1].attackMs = v > 0.5f ? 100.0f : 200.0f; pair_[1].releaseMs = v > 0.5f ? 100.0f : 8000.0f; break;
    case kSensor1: case kSensor2: case kSensor3: case kSensor4: {
      Pair& pr = pair_[(id - kSensor1) >> 1];
      Line& e = pr.env[(id - kSensor1) & 1];
      if (v > 0.5f) e.to(1.0f, pr.attackMs, fs_); else e.to(0.0f, pr.releaseMs, fs_);
      break;
    }
    case kVolume: vol_.to(v * v, kRampMs, fs_); break;
    case kDistDrive: {
      const float g = std::pow(10.0f, (std::pow(3.0f, 2.0f * v + 1.0f) + 3.0f) / 20.0f);
      drive_.to(g, kRampMs, fs_);
      break;
    }
    case kDistMix: dmix_.to(v, kRampMs, fs_); break;
    case kDelayMod: dMod_.to(v * v * 10.0f, kRampMs, fs_); break;
    case kDelayFb: dFb_.to(v * std::pow(2.0f, 2.0f * v), kRampMs, fs_); break;
    case kDelayMix: dMix_.to(v, kRampMs, fs_); break;
    case kDelayTime: dTime_.to(tMinMs_ * std::pow(tMaxMs_ / tMinMs_, v), kRampMs, fs_); break;
    case kLfoFreqA: lfoFa_.to(mtof(127.0f * v * v - 75.0f), kRampMs, fs_); break;
    case kLfoFreqB: lfoFb_.to(mtof(127.0f * v * v - 75.0f), kRampMs, fs_); break;
    default: break;
  }
}

void Engine::process(float* out, int n) {
  int done = 0;
  while (done < n) {
    const int k = std::min(kCtl, n - done);
    controlStep(k);
    renderChunk(out + done, k);
    done += k;
  }
}

// Advance every ramp by k samples and derive everything that does not need to change per sample.
void Engine::controlStep(int k) {
  Ctl& c = c_;
  const float pitch = pitch_.advance(k), hold = hold_.advance(k);
  for (int i = 0; i < 4; ++i) c.f[i] = tuneLine_[i].advance(k) * pitch;
  const bool fbOn = p_[kTotalFb] > 0.5f;
  for (int p = 0; p < 2; ++p) {
    Pair& pr = pair_[p];
    const int src = static_cast<int>(p_[p == 0 ? kSource12 : kSource34] + 0.5f);
    c.gv[p] = 0.001f + 0.999f * (src == 2);
    c.g0[p] = 0.001f + 0.999f * (src == 0);
    c.gt[p] = 0.001f + 0.999f * fbOn;
    c.gl[p] = 0.001f + 0.999f * (!fbOn);
    c.depth[p] = pr.mod.advance(k);
    c.sharp[p] = pr.sharp.advance(k);
    const float vamt = pr.vibAmt.advance(k);
    float vib = 0.0f;
    if (vamt != 0.0f) {
      pr.vibPh += pr.vibRate * static_cast<float>(k) / fs_; if (pr.vibPh >= 1.0f) pr.vibPh -= 1.0f;
      vib = sinTurns(pr.vibPh) * vamt;
    }
    c.fMul[p] = 1.0f + 0.005f * vib;
    c.skew[p] = 0.53125f + 0.025f * vib;
    for (int j = 0; j < 2; ++j) {
      Line& e = pr.env[j];
      const float ev = e.advance(k);
      c.gate[p * 2 + j] = std::min(1.0f, ev * ev + hold);
      c.bump[p * 2 + j] = (e.n > 0 || ev != 0.0f) ? 0.5f * sinTurns(ev) : 0.0f;   // exactly 0 when resting at 0 or 1
    }
  }
  c.tms = dTime_.advance(k); c.dfb = dFb_.advance(k); c.dmx = dMix_.advance(k); c.dmod = dMod_.advance(k);
  c.invFbNorm = 1.0f / std::max(1.5f, c.dfb);
  c.drive = drive_.advance(k); c.invGclip = 1.0f / clampf(c.drive, 1.0f, 4.0f);
  c.dmix = dmix_.advance(k); c.vol = vol_.advance(k);
  c.lfoFa = lfoFa_.advance(k); c.lfoFb = lfoFb_.advance(k);
  c.wave1 = p_[kDelayWaveform] > 0.5f; c.dsrc = static_cast<int>(p_[kDelaySource] + 0.5f);
  c.andOr = p_[kLfoAndOr] > 0.5f; c.link = p_[kLfoLink];
}

void Engine::renderChunk(float* out, int k) {
  const Ctl& c = c_;
  const float fs = fs_, invFs = 1.0f / fs_;
  const int mask = kRing - 1;
  Hip sendHp; sendHp.setCutoff(3.0f, fs); const float cSend = sendHp.coef;
  Hip h5; h5.setCutoff(5.0f, fs); const float cH5 = h5.coef;
  Hip h10; h10.setCutoff(10.0f, fs); const float cH10 = h10.coef;
  Hip h20; h20.setCutoff(20.0f, fs); const float cH20 = h20.coef;
  Lop lp6k; lp6k.setCutoff(6000.0f, fs); const float cL6k = lp6k.coef;
  Lop lp689; lp689.setCutoff(689.0f, fs); const float cL689 = lp689.coef;
  const int minDelaySamples = 64;
  const float dMinS = static_cast<float>(minDelaySamples), dMaxS = static_cast<float>(kCap - 6);

  for (int s = 0; s < k; ++s) {
    // ---- hyper LFO ----
    lfoPhA_ += c.lfoFa * invFs; if (lfoPhA_ >= 1.0f) lfoPhA_ -= 1.0f;
    const float sqA = (lfoPhA_ < 0.25f || lfoPhA_ >= 0.75f) ? 1.0f : -1.0f;   // = clip(cos*1000, -1, 1) to within 1e-3 of a cycle
    lfoPhB_ += c.lfoFb * (1.0f + 0.5f * sqA * c.link) * invFs; if (lfoPhB_ >= 1.0f) lfoPhB_ -= 1.0f;
    const float sqB = (lfoPhB_ < 0.25f || lfoPhB_ >= 0.75f) ? 1.0f : -1.0f;
    const float lfoSq = c.andOr ? sqA * sqB : 0.5f * (sqA + sqB);
    const float lfoTri = std::fabs(lfoPhA_ - 0.5f) + std::fabs(lfoPhB_ - 0.5f);
    const float lfoUni = 0.5f * (1.0f + 0.5f * (sqA + sqB));
    const float tfb = sendRing_[2][(ringPos_ - tfbD_ + kRing) & mask];

    // ---- voice pairs (pair 0 first unless pair 0 must read pair 1 in the same sample) ----
    float pairOut[2];
    const int order0 = crossD_[0] == 0 ? 1 : 0;
    for (int oi = 0; oi < 2; ++oi) {
      const int p = oi == 0 ? order0 : 1 - order0;
      Pair& pr = pair_[p];
      const int D = crossD_[p];
      const float other = D == 0 ? curSend_[1 - p] : sendRing_[1 - p][(ringPos_ - D + kRing) & mask];
      const float modSrc = other * c.gv[p] + (tfb * c.gt[p] + lfoSq * c.gl[p]) * c.g0[p];
      const float m = modSrc * c.depth[p];
      const float sharp = c.sharp[p], isharp = 1.0f - sharp;
      float y = 0;
      for (int j = 0; j < 2; ++j) {
        const float base = c.f[p * 2 + j] * c.fMul[p];
        Osc& o = pr.osc[j];
        o.run(base + base * m, c.skew[p], fs, invFs);
        y += ((o.outSq * sharp + o.outTri * isharp) + c.bump[p * 2 + j]) * c.gate[p * 2 + j];
      }
      pairOut[p] = y;
      const float nw = y + cSend * pr.hpX;       // hip~ 3 -> send~
      const float ho = nw - pr.hpX;
      pr.hpX = flush(nw);
      sendRing_[p][ringPos_] = ho; curSend_[p] = ho;
    }
    const float xin = (pairOut[0] + pairOut[1]) * 0.333333f;

    // ---- delay ----
    int selfIdx = dw_ - minDelaySamples; if (selfIdx < 0) selfIdx += kCap;
#if LYRA4_DELAY_INT16
    const float selfTap = dbuf_[selfIdx] * (LYRA4_DELAY_HEADROOM / 32767.0f);
#else
    const float selfTap = dbuf_[selfIdx];
#endif
    selfLp_ += cL689 * (0.5f * selfTap - selfLp_); selfLp_ = flush(selfLp_);
    float modv = 0;
    if (c.dsrc == 2) modv = c.wave1 ? lfoUni : lfoTri;
    else if (c.dsrc == 0) modv = selfLp_;
    float dsamp = (c.tms + modv * c.dmod) * 0.001f * fs;
    dsamp = clampf(dsamp, dMinS, dMaxS);
    float rp = static_cast<float>(dw_) - dsamp;
    if (rp < 0.0f) rp += static_cast<float>(kCap);
    const int i0 = static_cast<int>(rp); const float fr = rp - static_cast<float>(i0);
    auto rd_ = [&](int i) -> float {
      if (i < 0) i += kCap; else if (i >= kCap) i -= kCap;
#if LYRA4_DELAY_INT16
      return dbuf_[i] * (LYRA4_DELAY_HEADROOM / 32767.0f);
#else
      return dbuf_[i];
#endif
    };
#if LYRA4_DELAY_LAGRANGE
    const float ym1 = rd_(i0 - 1), y0 = rd_(i0), y1 = rd_(i0 + 1), y2 = rd_(i0 + 2);
    const float tap = y0 + fr * (0.5f * (y1 - ym1) + fr * ((ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2) + fr * 0.5f * (3.0f * (y0 - y1) + y2 - ym1)));
    // (Catmull-Rom cubic: same cost class as Pd's 4-point Lagrange, smoother passband.)
#else
    const float tap = rd_(i0) + fr * (rd_(i0 + 1) - rd_(i0));
#endif
    float t1; { const float nw = tap + cH5 * dHp_x; t1 = nw - dHp_x; dHp_x = flush(nw); }    // hip~ 5
    dLp_ += cL6k * (t1 - dLp_); dLp_ = flush(dLp_);                                           // lop~ 6000
    const float eOut = expander_.run(comp_.run(dLp_));    // compressor -> expander (traced; see lyra4_blocks.h)
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    const float noise = (static_cast<float>(rng_) * (1.0f / 2147483648.0f) - 1.0f) * 0.001f;
    const float wr = xin + noise + eOut * c.dfb;
#if LYRA4_DELAY_INT16
    dbuf_[dw_] = static_cast<int16_t>(clampf(wr * (32767.0f / LYRA4_DELAY_HEADROOM), -32767.0f, 32767.0f));
#else
    dbuf_[dw_] = wr;
#endif
    if (++dw_ >= kCap) dw_ = 0;
    const float wet = padeTanh(eOut * c.invFbNorm);
    const float dOut = xin * (1.0f - c.dmx) + c.dmx * wet;

    // ---- master output ----
    float a20; { const float nw = dOut * c.drive + cH20 * mHp20_x; a20 = nw - mHp20_x; mHp20_x = flush(nw); }
    const float T = padeTanh(a20);
    const float T2 = T * T, T4 = T2 * T2, T8 = T4 * T4, T16 = T8 * T8;
    const float z = T + 0.25f * (T16 * T8 * T4 * T2 * T);     // + T^31 / 4 (pow~ 31 keeps the sign)
    float a10; { const float nw = z + cH10 * mHp10_x; a10 = nw - mHp10_x; mHp10_x = flush(nw); }
    const float u = padeTanh(dOut * (1.0f - c.dmix) + (a10 * c.invGclip + 0.1f * dOut) * c.dmix);
    float tf; { const float nw = u + cSend * tfbHp_x; tf = nw - tfbHp_x; tfbHp_x = flush(nw); }
    sendRing_[2][ringPos_] = tf;
    ringPos_ = (ringPos_ + 1) & mask;
    float o = u * c.vol;
    if (opt_.dcBlock) {
      const float y = o - dcX_ + 0.99974f * dcY_;
      dcX_ = o; dcY_ = flush(y); o = y;
    }
    if (!std::isfinite(o)) { ++recoveries_; reset(); o = 0.0f; }   // recovery: clear every state, count it, output silence
    out[s] = clampf(o, -4.0f, 4.0f);
  }
}

}  // namespace lyra4
