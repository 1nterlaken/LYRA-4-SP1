#include "sp1_lyra.h"
#include "lyra_ctl.h"
#include "lyra4_engine.h"
#include <string.h>

#define LYRA_ID_CHECK(a, b) static_assert((int)(a) == (int)(b), "lyra_ctl.h's parameter ids drifted from lyra4::Param")
LYRA_ID_CHECK(LYRA_P_COUNT, lyra4::kParamCount);
LYRA_ID_CHECK(LYRA_P_TUNE1, lyra4::kTune1);       LYRA_ID_CHECK(LYRA_P_PITCH, lyra4::kPitch);
LYRA_ID_CHECK(LYRA_P_MOD12, lyra4::kMod12);       LYRA_ID_CHECK(LYRA_P_SHARP34, lyra4::kSharp34);
LYRA_ID_CHECK(LYRA_P_SOURCE12, lyra4::kSource12); LYRA_ID_CHECK(LYRA_P_SOURCE34, lyra4::kSource34);
LYRA_ID_CHECK(LYRA_P_TOTALFB, lyra4::kTotalFb);   LYRA_ID_CHECK(LYRA_P_HOLD, lyra4::kHold);
LYRA_ID_CHECK(LYRA_P_VIBRATO, lyra4::kVibrato);   LYRA_ID_CHECK(LYRA_P_FAST12, lyra4::kFast12);
LYRA_ID_CHECK(LYRA_P_FAST34, lyra4::kFast34);     LYRA_ID_CHECK(LYRA_P_SENSOR1, lyra4::kSensor1);
LYRA_ID_CHECK(LYRA_P_SENSOR4, lyra4::kSensor4);   LYRA_ID_CHECK(LYRA_P_VOLUME, lyra4::kVolume);
LYRA_ID_CHECK(LYRA_P_DRIVE, lyra4::kDistDrive);   LYRA_ID_CHECK(LYRA_P_DMIX, lyra4::kDistMix);
LYRA_ID_CHECK(LYRA_P_DWAVE, lyra4::kDelayWaveform); LYRA_ID_CHECK(LYRA_P_DSOURCE, lyra4::kDelaySource);
LYRA_ID_CHECK(LYRA_P_DMOD, lyra4::kDelayMod);     LYRA_ID_CHECK(LYRA_P_DFB, lyra4::kDelayFb);
LYRA_ID_CHECK(LYRA_P_DMIXD, lyra4::kDelayMix);    LYRA_ID_CHECK(LYRA_P_DTIME, lyra4::kDelayTime);
LYRA_ID_CHECK(LYRA_P_LFOA, lyra4::kLfoFreqA);     LYRA_ID_CHECK(LYRA_P_LFOB, lyra4::kLfoFreqB);
LYRA_ID_CHECK(LYRA_P_LFOLINK, lyra4::kLfoLink);   LYRA_ID_CHECK(LYRA_P_LFOANDOR, lyra4::kLfoAndOr);

namespace {
lyra4::HalfRateEngine g_eng;

struct Pub {
	float p[lyra4::kParamCount];
	bool quantize;
};
Pub g_pub[2];
volatile int g_pub_idx;             /* which buffer the audio thread reads */
volatile uint32_t g_pub_seq;        /* bumped after a publish */
uint32_t g_seen_seq;                /* audio thread only */

void apply_all()
{
	const Pub &s = g_pub[g_pub_idx];
	lyra4::Options o;
	o.quantizeTuning = s.quantize;
	g_eng.engine().setOptions(o);
	for (int i = 0; i < lyra4::kParamCount; ++i) {
		if (g_eng.engine().param(i) != s.p[i]) {
			g_eng.engine().setParam(i, s.p[i]);
		}
	}
}
}  // namespace

extern "C" void sp1_lyra_init(void)
{
	g_eng.init(48000.0f);
	memset(g_pub, 0, sizeof(g_pub));
	for (int i = 0; i < lyra4::kParamCount; ++i) {
		g_pub[0].p[i] = g_pub[1].p[i] = g_eng.engine().param(i);
	}
	g_pub_idx = 0;
	g_pub_seq = g_seen_seq = 0;
}

extern "C" void sp1_lyra_reset(void)
{
	/* Audio thread is parked: a full re-init clears the delay line and every state. */
	g_eng.init(48000.0f);
	g_seen_seq = g_pub_seq - 1u;     /* apply the published set at the next block */
}

extern "C" void sp1_lyra_publish(const float *params, bool quantize)
{
	const int w = 1 - g_pub_idx;
	memcpy(g_pub[w].p, params, sizeof(float) * lyra4::kParamCount);
	g_pub[w].quantize = quantize;
	g_pub_idx = w;                   /* the audio thread outranks main: it never sees half a copy */
	g_pub_seq = g_pub_seq + 1u;
}

extern "C" void sp1_lyra_render(int16_t *out, uint32_t frames)
{
	if (g_seen_seq != g_pub_seq) {
		g_seen_seq = g_pub_seq;
		apply_all();
	}
	float tmp[96];
	if (frames > 96u) {
		frames = 96u;
	}
	g_eng.process(tmp, (int)frames);
	for (uint32_t i = 0; i < frames; ++i) {
		float v = tmp[i] * 32767.0f;
		if (!(v < 32767.0f)) { v = 32767.0f; }
		if (!(v > -32767.0f)) { v = -32767.0f; }
		out[i] = (int16_t)v;
	}
}
