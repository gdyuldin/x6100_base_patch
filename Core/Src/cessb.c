#include "cessb.h"

#include <dsp/controller_functions.h>

#include "external.h"
#include "utils.h"
#include "math/const.h"
#include "math/fast_math.h"

#define DELAY 32
#define NTAPS (DELAY * 2 + 1)
#define SAMPLING_RATE 12500.0f

static void calc_fir_lowpass_coefficients(float *coeffs, uint32_t num_taps, float cut_off_freq);

static CCMRAM struct {
    // Filters data
    arm_fir_instance_f32 i_lpf;
    float i_state[NTAPS];
    arm_fir_instance_f32 q_lpf;
    float q_state[NTAPS];
    float coeffs[NTAPS];

    // Baseband shift data
    float phase_deg;
    float phase_step;

    // Delay line
    float dly_i[DELAY];
    float dly_q[DELAY];
    uint8_t dly_n;

    // Clipping threshold - for prevent artifacts
    float clipping_th;

    // Power improvement before clipping
    float power_up;
    bool on;
} state;

void cessb_init(void) {
    // Init I and Q lpf
    arm_fir_init_f32(&state.i_lpf, NTAPS, state.coeffs, state.i_state, 1);
    ext_arm_fill_f32(0.0f, state.i_state, ARRAY_SIZE(state.i_state));

    arm_fir_init_f32(&state.q_lpf, NTAPS, state.coeffs, state.q_state, 1);
    ext_arm_fill_f32(0.0f, state.q_state, ARRAY_SIZE(state.q_state));

    cessb_update_filter(160, 3000);
    state.phase_deg = 0.0f;
    state.clipping_th = db2lin(1.0f);
    state.power_up = db2lin(3.7f);
    state.on = false;

    state.dly_n = 0;
    ext_arm_fill_f32(0.0f, state.dly_i, ARRAY_SIZE(state.dly_i));
    ext_arm_fill_f32(0.0f, state.dly_q, ARRAY_SIZE(state.dly_q));
}

void cessb_update_filter(uint16_t low, uint16_t high) {
    uint16_t bw = high - low;
    float cutoff = 0.5f * bw;
    float shift_freq = cutoff + low;
    calc_fir_lowpass_coefficients(state.coeffs, NTAPS, cutoff);
    state.phase_step = 360.0f * shift_freq / SAMPLING_RATE;
}

void cessb_set_params(bool on, float power_up_db) {
    bool prev_on = state.on;
    if (on != prev_on) {
        state.on = on;
        if (on) {
            // Reset delay line
            state.dly_n = 0;
            ext_arm_fill_f32(0.0f, state.dly_i, ARRAY_SIZE(state.dly_i));
            ext_arm_fill_f32(0.0f, state.dly_q, ARRAY_SIZE(state.dly_q));
            // Reset LPF state
            ext_arm_fill_f32(0.0f, state.i_state, ARRAY_SIZE(state.i_state));
            ext_arm_fill_f32(0.0f, state.q_state, ARRAY_SIZE(state.q_state));
        }
    }
    state.power_up = db2lin(power_up_db);
}

void cessb_process(float *i_p, float *q_p) {
    USE_OEM_MODULATION_AS(pMode);

    if (!state.on) {
        return;
    }

    // shift baseband
    float i = *i_p;
    float q = *q_p;
    float shift_real, shift_imag;
    arm_sin_cos_f32(state.phase_deg, &shift_imag, &shift_real);
    if (*pMode == MOD_LSB) {
        state.phase_deg += state.phase_step;
        if (state.phase_deg > 360.0f) {
            state.phase_deg -= 360.0f;
        }
    } else {
        state.phase_deg -= state.phase_step;
        if (state.phase_deg < 0.0f) {
            state.phase_deg += 360.0f;
        }
    }
    cmplx_mul(i, q, shift_real, shift_imag, &i, &q);

    // TODO: add up gain
    i = i * state.power_up;
    q = q * state.power_up;

    // Delay for I and Q
    float i_d = state.dly_i[state.dly_n];
    float q_d = state.dly_q[state.dly_n];
    state.dly_i[state.dly_n] = i;
    state.dly_q[state.dly_n] = q;
    state.dly_n = (state.dly_n + 1) & (DELAY - 1);

    // get magnitude
    float mag = sqrtf(i * i + q * q);

    // Get correction
    float i_corr = 0.0f, q_corr=0.0f;
    if (mag > state.clipping_th) {
        i_corr = i * state.clipping_th / mag - i;
        q_corr = q * state.clipping_th / mag - q;
    }

    // Apply LPF
    ext_arm_fir_f32(&state.i_lpf, &i_corr, &i_corr, 1);
    ext_arm_fir_f32(&state.q_lpf, &q_corr, &q_corr, 1);

    i = i_d + i_corr;
    q = q_d + q_corr;

    // unshift baseband
    cmplx_mul(i, q, shift_imag, shift_real, i_p, q_p);
}

static void calc_fir_lowpass_coefficients(float *coeffs, uint32_t num_taps, float cut_off_freq) {
    float fc = cut_off_freq / SAMPLING_RATE;
    size_t M = num_taps - 1; // Filter order
    float sum = 0.0f;
    float val;

    // Filter coefficients has a symmetry, calc only for first half
    for (size_t n = 0; n < M / 2; n++) {
        float m = n - M / 2.0f;
        // Get coeff with sinc function
        val = ext_arm_sin_f32(2.0f * M_PI_F * fc * m) / (M_PI_F * m);

        // Apply Hamming Window
        float hamming = 0.54f - 0.46f * ext_arm_cos_f32((2.0f * M_PI_F * n) / M);
        val *= hamming;
        coeffs[n] = val;
        coeffs[num_taps - n - 1] = val;

        // Update sum
        sum += 2.0f * val;
    }

    // Fill middle value
    val = 2.0f * fc;
    sum += val;
    coeffs[M / 2] = val;

    float norm = 1.0f / sum;

    for (size_t i = 0; i < num_taps; i++)
    {
        coeffs[i] *= norm;
    }

}
