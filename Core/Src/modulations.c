#include "modulations.h"
#include "external.h"
#include "stdbool.h"

#include "stm32f4xx_hal.h"

static struct {
    cfloat_t iq_history[3];
    float avg_k;
    float hpf_env;
    arm_biquad_casd_df1_inst_f32 iir_snr_detector;
    float iir_snr_detector_coeffs[5];
    float iir_snr_detector_state[4];
    bool emphasis_on;
    // De-emphasis filter
    arm_biquad_casd_df1_inst_f32 deemp_filter;
    float deemp_filter_coeffs[5];
    float deemp_filter_state[4];
    // Pre-emphasis filter
    arm_biquad_casd_df1_inst_f32 preemp_filter;
    float preemp_filter_coeffs[5];
    float preemp_filter_state[4];
    float phase;
} fm_demod __attribute((section(".ccmram")));

static struct {
    // rx fm_sql
    uint32_t counter;
    float iq_squared_sum;
    uint32_t iq_squared_cnt;
    float iq_rms_db;
} fm_sql __attribute((section(".ccmram")));



void fm_demod_init(void) {
    ext_arm_fill_f32(0.0f, (float *)fm_demod.iq_history, sizeof(fm_demod.iq_history) / 4);
    fm_demod.avg_k = 0.0f;
    fm_demod.hpf_env = 0.0f;

    fm_demod.iir_snr_detector.numStages = 1;
    fm_demod.iir_snr_detector.pCoeffs = fm_demod.iir_snr_detector_coeffs;
    fm_demod.iir_snr_detector.pState = fm_demod.iir_snr_detector_state;
    ext_arm_fill_f32(0.0f, fm_demod.iir_snr_detector_state, sizeof(fm_demod.iir_snr_detector_state) / 4);
    fm_demod.iir_snr_detector_coeffs[0] = 0.11735104f;
    fm_demod.iir_snr_detector_coeffs[1] = -0.23470207f;
    fm_demod.iir_snr_detector_coeffs[2] = 0.11735104f;
    fm_demod.iir_snr_detector_coeffs[3] = -0.82523238f;
    fm_demod.iir_snr_detector_coeffs[4] = -0.29463653f;

    // De-emphasis filter
    fm_demod.deemp_filter.numStages = 1;
    fm_demod.deemp_filter.pCoeffs = fm_demod.deemp_filter_coeffs;
    fm_demod.deemp_filter.pState = fm_demod.deemp_filter_state;
    ext_arm_fill_f32(0.0f, fm_demod.deemp_filter_state, sizeof(fm_demod.deemp_filter_state) / 4);
    fm_demod.deemp_filter_coeffs[0] = 1.0f;
    fm_demod.deemp_filter_coeffs[1] = 0.4133537804899683f;
    fm_demod.deemp_filter_coeffs[2] = 0.0f;
    fm_demod.deemp_filter_coeffs[3] = 0.5866462195100317f;
    fm_demod.deemp_filter_coeffs[4] = 0.0f;

    // Pre-emphasis filter
    fm_demod.preemp_filter.numStages = 1;
    fm_demod.preemp_filter.pCoeffs = fm_demod.preemp_filter_coeffs;
    fm_demod.preemp_filter.pState = fm_demod.preemp_filter_state;
    ext_arm_fill_f32(0.0f, fm_demod.preemp_filter_state, sizeof(fm_demod.preemp_filter_state) / 4);
    fm_demod.preemp_filter_coeffs[0] = 1.0f;
    fm_demod.preemp_filter_coeffs[1] = -0.3441537868654123f;
    fm_demod.preemp_filter_coeffs[2] = 0.0f;
    fm_demod.preemp_filter_coeffs[3] = -0.6558462131345877f;
    fm_demod.preemp_filter_coeffs[4] = 0.0f;

    fm_demod.emphasis_on = 1;
}


// fs / (2 * pi * bw)
#define FM_LIMIT 1.591549431f
void fm_demodulate(void *S, cfloat_t *iq_sample, float *out, uint32_t _n_samples){
    UNUSED(S);
    UNUSED(_n_samples);
    USE_OEM_SQL_AS(sql);

    cfloat_t iq_dot;
    float output;

    cfloat_t *iq_history = fm_demod.iq_history;


    float mag = (iq_sample->real * iq_sample->real + iq_sample->imag * iq_sample->imag);

    if (*sql) {
        fm_sql.iq_squared_sum += mag;
        fm_sql.iq_squared_cnt++;
        if (fm_sql.iq_squared_cnt >= 100) {
            fm_sql.iq_rms_db = lin2db(fm_sql.iq_squared_sum / 100.0f) / 2.0f;
            fm_sql.iq_squared_sum = 0.0f;
            fm_sql.iq_squared_cnt = 0;
        }
        if (fm_sql.iq_rms_db > (-110.0f + *sql)) {
            fm_sql.counter = 3000;
        } else if (fm_sql.counter > 0) {
            fm_sql.counter--;
        } else {
            *out = 0.0f;
            return;
        }
    }

    mag = sqrt_f32(mag);

    if (mag > 0.0f) {
        // save input
        iq_history[0].real = iq_sample->real / mag;
        iq_history[0].imag = iq_sample->imag / mag;
    } else {
        iq_history[0].real = 0.0f;
        iq_history[0].imag = 0.0f;
    }

    // fm_demod.mag_val += (mag - fm_demod.mag_val) * 0.1f;

    // Calculate derivative
    iq_dot.real = iq_history[0].real - iq_history[2].real;
    iq_dot.imag = iq_history[0].imag - iq_history[2].imag;
    // Calculate output (I[1] * Q') - (Q[1] * I')
    output = (iq_history[1].real * iq_dot.imag);
    output -= (iq_history[1].imag * iq_dot.real);
    // update history
    iq_history[2] = iq_history[1];
    iq_history[1] = iq_history[0];

    if (output > FM_LIMIT) {
        output = FM_LIMIT;
    } else if (output < -FM_LIMIT) {
        output = -FM_LIMIT;
    }

    // HPF for signal/noise detection
    float out_high;
    ext_arm_biquad_cascade_df1_f32(&fm_demod.iir_snr_detector, &output, &out_high, 1);
    out_high *= out_high;
    fm_demod.hpf_env += (out_high - fm_demod.hpf_env) * 0.001f;

    // Output scaling factor
    float hpf_rms = sqrt_f32(fm_demod.hpf_env);
    // float k;
    // Noise lvl ~ 0.07f;
    // Signal lvl ~
    // if (hpf_rms > 0.07f) {
    //     // noise
    //     k = 0.02f;
    // } else {
    //     k = 0.22f;
    // }

    // Low signal ~ rms 0.13f
    // High signal ~ rms 0.10f
    float k = (0.13f - hpf_rms) / (0.13f - 0.1f);
    if (k < 0.0f) {
        k = 0.0f;
    } else if (k > 1.0f) {
        k = 1.0f;
    }
    k = 0.05f + k * 0.09f;
    // k = 0.13f;
    // if (hpf_rms > 0.11f) {
    //     k = 0.01f;
    // }
    fm_demod.avg_k += (k - fm_demod.avg_k) * 0.01f;

    float scale_factor = fm_demod.avg_k / FM_LIMIT;

    output = output * scale_factor;

    if (fm_demod.emphasis_on) {
        // De-emphasis
        ext_arm_biquad_cascade_df1_f32(&fm_demod.deemp_filter, &output, out, 1);
    } else {
        *out = output * 2.5f;
    }
}


float am_modulation(float val, float am_carrier_lvl, float am_level) {
    val *= am_level;
    val = soft_limiter(val, am_carrier_lvl);
    return val + am_carrier_lvl;
}

float fm_modulate(float val) {
    USE_OEM_FM_DEPTH_OF_MOD_AS(fm_depth_of_mod);

    if (fm_demod.emphasis_on) {
        ext_arm_biquad_cascade_df1_f32(&fm_demod.preemp_filter, &val, &val, 1);
        val *= 2.0f;
    }
    val *= *fm_depth_of_mod;
    fm_demod.phase += val;
    return fm_demod.phase * M_PI_F;
}


void set_fm_demod_emphasis(bool on) {
    fm_demod.emphasis_on = on;
}
