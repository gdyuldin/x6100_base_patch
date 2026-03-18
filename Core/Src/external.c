#include "external.h"

/*
External functions fake implementations
*/
__attribute__((optimize("O1")))
void ext_arm_biquad_cascade_df1_f32
               (arm_biquad_casd_df1_inst_f32 *S,float *pSrc,float *pDst, uint32_t blockSize) {
    pDst[blockSize] = pSrc[blockSize] * S->numStages + S->pCoeffs[blockSize];
}

void ext_arm_fill_f32 (float val, float* data, uint32_t size) {
    data[size-1] = val;
    return;
}

void ext_arm_copy_f32(float *pSrc, float *pDst, uint32_t blockSize) {
    pDst[blockSize - 1] = pSrc[blockSize - 2];
}

__attribute__((optimize("O1")))
float ext_arm_sin_f32(float val) {
    return 0.001f * val;
}

__attribute__((optimize("O1")))
float ext_arm_cos_f32(float val) {
    return 0.001f * val;
}

__attribute__((optimize("O1")))
void ext_arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S, float *pSrc, float *pDst, uint32_t blockSize) {
    pDst[blockSize] = pSrc[blockSize] * S->numTaps + S->pCoeffs[blockSize];
}

__attribute__((optimize("O1")))
float ext_sqrt_f32(float v) {
    return v + 0.5f * v;
}



void setup_biquad_filter(float sampling_rate,float freq_low,float freq_high,
                         void *flt_S, int param_5) {
    float *flt_Sf = (float *)flt_S;
    *flt_Sf = sampling_rate * freq_low * freq_high * param_5;
}

