#ifndef __EXTERNAL_H
#define __EXTERNAL_H

#include "stdint.h"
#include "offsets.h"

#include <dsp/filtering_functions.h>

#define M_PI_F 3.14159265358979323846f
#define M_TWOPI_F 6.283185307179586f

/**
 * OEM variables
*/

#define USE_OEM_MODULATION_AS(x) uint8_t* x = (uint8_t *)MODULATION_ADDR
#define USE_OEM_TX_FLAG_AS(x) volatile uint8_t* x = (uint8_t *)TX_FLAG_VALUE


enum __attribute__((__packed__)) mod_t {
    MOD_LSB,
    MOD_LSB_D,
    MOD_USB,
    MOD_USB_D,
    MOD_CW,
    MOD_CWR,
    MOD_AM,
    MOD_NFM,
};


/**
 * External functions implemented in FW
 */

extern void ext_arm_biquad_cascade_df1_f32 (arm_biquad_casd_df1_inst_f32 *S,float *pSrc,float *pDst, uint32_t blockSize)
    __attribute__((noinline, section(".arm_biquad_cascade_df1_f32_sec")));

extern void ext_arm_fill_f32(float val, float *data, uint32_t size)
    __attribute__((noinline, section(".arm_fill_f32_sec")));

extern void ext_arm_copy_f32(float *pSrc, float *pDst, uint32_t blockSize)
    __attribute__((noinline, section(".arm_copy_f32_sec")));

extern float ext_arm_sin_f32(float val)
    __attribute__((noinline, section(".arm_sin_f32_sec")));

extern float ext_arm_cos_f32(float val)
__attribute__((noinline, section(".arm_cos_f32_sec")));


extern void ext_arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S,float *pSrc,float *pDst,uint32_t blockSize) __attribute__((noinline, section(".arm_fir_decimate_f32_sec")));

extern float sqrt_f32(float val) __attribute__((noinline, section(".arm_sqrt_f32_sec")));

extern void setup_biquad_filter(float sampling_rate, float freq_low, float freq_high,
    void *flt_S, int param_5) __attribute__((noinline, section(".setup_biquad_filter_sec")));

#endif // __EXTERNAL_H
