#include "anf.h"

#include "external.h"


/* ANF data */
struct {
    bool enabled;
    float an;
    float mean_squared;
} anf __attribute((section(".ccmram")));


/**
 * Adaptive notch filter
 */


void anf_update(void) {
    arm_biquad_casd_df1_inst_f32 *flt = (arm_biquad_casd_df1_inst_f32 *)ARM_BIQUAD_CASD_DF1_INST_VALUE;
    const float k = 3e-3f;

    if (anf.enabled) {
        float mean_squared = flt->pState[0] * flt->pState[0] + flt->pState[1] * flt->pState[1];
        anf.mean_squared += (mean_squared - anf.mean_squared) * 0.01f;

        // float width = 100.0f; // Hz
        const float gain = 0.9754784f;
        const float r = 0.974862f;

        if (!isfinite(flt->pState[2]) || !isfinite(flt->pState[3])) {
            flt->pState[0] = 0.0f;
            flt->pState[1] = 0.0f;
            flt->pState[2] = 0.0f;
            flt->pState[3] = 0.0f;
        }

        // estimate_an
        float an = anf.an;
        float gradient = flt->pState[2] * (gain * flt->pState[3] - flt->pState[1]);
        an = an - k * gradient / (anf.mean_squared + 1e-12f);

        an = MIN(1.9597101f, an);  // 400 Hz
        an = MAX(-0.85155857f, an);  // 4000 Hz

        anf.an = an;

        // update_coeffs
        float *coeffs = (float *)flt->pCoeffs;
        coeffs[1] = -an * gain;
        coeffs[3] = -flt->pCoeffs[1];

        if (coeffs[0] != gain) {
            coeffs[0] = gain;
            coeffs[2] = gain;
            coeffs[4] = -r * gain;
        }
    }
}


void set_anf_enable(bool on) {
    anf.enabled = on;
}
