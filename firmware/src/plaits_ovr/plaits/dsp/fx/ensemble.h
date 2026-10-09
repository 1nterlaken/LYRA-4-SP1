// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Ensemble FX.
//
// -----------------------------------------------------------------------------
//
// LYRA-4 (M3g/M3h): Plaits' ensemble (used only by the string machine engine)
// with two changes. Upstream: third_party/eurorack/plaits/dsp/fx/ensemble.h (Emilie
// Gillet, MIT, above). Shadows it through the include path; see
// firmware/CMakeLists.txt.
//
// M3h (Adara): TWO chorus taps per side instead of three. Upstream mixes, per side,
// its own line at the 0- and 120-degree LFO phases plus the OTHER line at 240
// degrees, each x0.33; here the 240-degree cross tap is dropped and the two
// remaining taps are x0.5, keeping the wet level. ~-37 instructions/sample; the
// chorus is 2-phase instead of 3-phase (a little more audible wobble).
//
// M3g: at amount == 0 -- the string machine's TIMBRE on its centre detent --
// upstream's mix is exactly dry (wet * 0 + dry * 1), so the modulated taps are
// skipped. The lines are still written and the LFO phases still advance, so when
// the chorus comes back it has exactly the history it would have had. Checked on
// the host (with three taps): bit-identical to upstream, dry and coming back in.
//
// (Ramping the LFOs across the block instead of looking them up per sample saved
// only ~18 of ~595 instructions and changed the sound by -33 dB -- upstream's LFO
// is a stepped, uninterpolated table read, part of its character -- so it was
// not kept.)

#ifndef PLAITS_DSP_FX_ENSEMBLE_H_
#define PLAITS_DSP_FX_ENSEMBLE_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/dsp/fx/fx_engine.h"
#include "plaits/resources.h"

namespace plaits {

class Ensemble {
 public:
  typedef FxEngine<1024, FORMAT_32_BIT> E;
  static const bool kSp1Override = true;   // checked in sp1_synth.cc

  Ensemble() { }
  ~Ensemble() { }

  void Init(E::T* buffer) {
    engine_.Init(buffer);
    phase_1_ = 0;
    phase_2_ = 0;
    amount_ = 0.0f;
    depth_ = 0.0f;
  }

  void Reset() {
    engine_.Clear();
  }

  void Process(float* left, float* right, size_t size) {
    const uint32_t kInc1 = 67289;   // 0.75 Hz
    const uint32_t kInc2 = 589980;  // 6.57 Hz

    typedef E::Reserve<511, E::Reserve<511> > Memory;
    E::DelayLine<Memory, 0> line_l;
    E::DelayLine<Memory, 1> line_r;
    E::Context c;

    if (amount_ == 0.0f) {
      // Upstream's output is exactly the dry input here: keep feeding the lines
      // (so the chorus comes back in with the same history as upstream's) but skip
      // the six modulated taps.
      phase_1_ += kInc1 * static_cast<uint32_t>(size);
      phase_2_ += kInc2 * static_cast<uint32_t>(size);
      while (size--) {
        engine_.Start(&c);
        c.Read(*left++, 1.0f);
        c.Write(line_l, 0.0f);
        c.Read(*right++, 1.0f);
        c.Write(line_r, 0.0f);
      }
      return;
    }

    while (size--) {
      engine_.Start(&c);
      float dry_amount = 1.0f - amount_ * 0.5f;

      // Update LFO.
      const uint32_t one_third = 1417339207UL;

      phase_1_ += kInc1;
      phase_2_ += kInc2;
      float slow_0 = SineRaw(phase_1_);
      float slow_120 = SineRaw(phase_1_ + one_third);
      float fast_0 = SineRaw(phase_2_);
      float fast_120 = SineRaw(phase_2_ + one_third);

      // Max deviation: 176
      float a = depth_ * 160.0f;
      float b = depth_ * 16.0f;

      float mod_1 = slow_0 * a + fast_0 * b;
      float mod_2 = slow_120 * a + fast_120 * b;


      float wet = 0.0f;

      // Sum L & R channel to send to chorus line.
      c.Read(*left, 1.0f);
      c.Write(line_l, 0.0f);
      c.Read(*right, 1.0f);
      c.Write(line_r, 0.0f);

      // LYRA-4 (M3h): two taps per side (upstream: three, the third being the
      // other line at mod_3), each at 0.5 so the wet level matches (3 x 0.33).
      c.Interpolate(line_l, mod_1 + 192, 0.5f);
      c.Interpolate(line_l, mod_2 + 192, 0.5f);
      c.Write(wet, 0.0f);
      *left = wet * amount_ + *left * dry_amount;

      c.Interpolate(line_r, mod_1 + 192, 0.5f);
      c.Interpolate(line_r, mod_2 + 192, 0.5f);
      c.Write(wet, 0.0f);
      *right = wet * amount_ + *right * dry_amount;
      left++;
      right++;
    }
  }

  inline void set_amount(float amount) {
    amount_ = amount;
  }

  inline void set_depth(float depth) {
    depth_ = depth;
  }

 private:
  E engine_;

  float amount_;
  float depth_;

  uint32_t phase_1_;
  uint32_t phase_2_;

  DISALLOW_COPY_AND_ASSIGN(Ensemble);
};

}  // namespace plaits

#endif  // PLAITS_DSP_FX_ENSEMBLE_H_
