#include "external.h"

/*
External functions fake implementations
*/
void ext_arm_biquad_cascade_df1_f32
               (arm_biquad_casd_df1_inst_f32 *S,float *pSrc,float *pDst, uint32_t blockSize) {
    UNUSED(S);
    UNUSED(pSrc);
    UNUSED(pDst);
    UNUSED(blockSize);
}

void ext_arm_fill_f32 (float val, float* data, uint32_t size) {
    UNUSED(val);
    UNUSED(data);
    UNUSED(size);
}

void ext_arm_copy_f32(const float *pSrc, float *pDst, uint32_t blockSize) {
    UNUSED(pSrc);
    UNUSED(pDst);
    UNUSED(blockSize);
}


float ext_arm_sin_f32(float val) {
    UNUSED(val);
    return 0.0f;
}

float ext_arm_cos_f32(float val) {
    UNUSED(val);
    return 0.0f;
}

void ext_arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S, float *pSrc, float *pDst, uint32_t blockSize) {
    UNUSED(S);
    UNUSED(pSrc);
    UNUSED(pDst);
    UNUSED(blockSize);
}


void ext_arm_fir_f32 (const arm_fir_instance_f32 *S, const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    UNUSED(S);
    UNUSED(pSrc);
    UNUSED(pDst);
    UNUSED(blockSize);
}

float ext_sqrt_f32(float v) {
    UNUSED(v);
    return 0.0f;
}



void setup_biquad_filter(float sampling_rate,float freq_low,float freq_high,
                         void *flt_S, int param_5) {
    UNUSED(sampling_rate);
    UNUSED(freq_low);
    UNUSED(freq_high);
    UNUSED(flt_S);
    UNUSED(param_5);
}


void ext_write_i2c(void *i2c_typedef, uint32_t addr, uint8_t *data, uint32_t data_len, uint32_t timeout){
    UNUSED(i2c_typedef);
    UNUSED(addr);
    UNUSED(data);
    UNUSED(data_len);
    UNUSED(timeout);
}

void ext_setup_tx(void *struct_data, uint32_t flags, uint8_t tx) {
    UNUSED(struct_data);
    UNUSED(flags);
    UNUSED(tx);
}

void ext_set_aic_mic_power(uint32_t val) {
    UNUSED(val);
}

void ext_set_aic_micpga_volume(uint32_t ch, uint32_t val) {
    UNUSED(ch);
    UNUSED(val);
}

void ext_set_aic_input_routes(uint32_t ch, uint32_t input) {
    UNUSED(ch);
    UNUSED(input);
}
