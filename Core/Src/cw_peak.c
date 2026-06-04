#include "cw_peak.h"
#include "external.h"
#include "utils.h"
#include "math/const.h"

#define SAMPLING_RATE 12500

static CCMRAM struct {
    bool on;
    arm_biquad_casd_df1_inst_f32 filter;
    float filter_state[4];
    float filter_coeffs[5];
} state;


void cw_peak_init(void) {
    ext_arm_fill_f32(0.0f, state.filter_state, ARRAY_SIZE(state.filter_state));
    ext_arm_fill_f32(0.0f, state.filter_coeffs, ARRAY_SIZE(state.filter_coeffs));
    state.filter_coeffs[0] = 1.0f;

    state.filter.numStages = 1;
    state.filter.pState = state.filter_state;
    state.filter.pCoeffs = state.filter_coeffs;

    state.on = false;
}

void cw_peak_setup(bool on, float Q) {
    state.on = on;
    if (!on) {
        return;
    }

    USE_OEM_KEY_TONE_AS(keyTone);

    Q = MAX(0.1f, Q);

    float w0 = *keyTone * 2.0f / SAMPLING_RATE;

    float bw = w0 / Q;

    w0 *= M_PI_F;
    bw *= M_PI_F;

    float beta = ext_arm_sin_f32(bw * 0.5f);

    float gain = 1.0f / (1.0f + beta);

    // b0, b1, b2
    state.filter_coeffs[0] = (1.0f - gain) * (1.0f + log10f_c(Q));
    state.filter_coeffs[1] = 0.0f;
    state.filter_coeffs[2] = -state.filter_coeffs[0];

    // a1, a2
    state.filter_coeffs[3] = 2.0f * gain * ext_arm_cos_f32(w0);
    state.filter_coeffs[4] = 1.0f - 2.0f * gain;
}


float cw_peak_process(float sample) {
    USE_OEM_MODULATION_AS(pMode);
    if (!state.on) {
        return sample;
    }
    if ((*pMode != MOD_CW) && (*pMode != MOD_CWR)) {
        return sample;
    }
    float filtered;
    ext_arm_biquad_cascade_df1_f32(&state.filter, &sample, &filtered, 1);
    return filtered;
}
