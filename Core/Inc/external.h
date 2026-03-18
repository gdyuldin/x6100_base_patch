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
#define USE_OEM_SQL_AS(x) volatile uint8_t* x = (uint8_t *)SQL_VALUE
#define USE_OEM_FM_DEPTH_OF_MOD_AS(x) volatile float* x = (float *)FM_DEPTH_OF_MOD_VALUE
#define USE_OEM_FREQ_PLUS_RIT_AS(x) volatile uint32_t* x = (uint32_t *)FREQ_PLUS_RIT
#define USE_OEM_SAMPLES_COUNT_VALUE_AS(x) volatile uint32_t* x = (uint32_t *)SAMPLES_COUNT_VALUE
#define USE_OEM_NRE_AS(x) volatile uint8_t* x = (uint8_t *)NRE_FLAG

// I2C registers values start pointer
#define USE_OEM_I2C_REGS_AS(x) volatile uint32_t* x = (uint32_t *)I2C_REGS_ADDR

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

extern float ext_sqrt_f32(float val)
__attribute__((noinline, section(".arm_sqrt_f32_sec")));


extern void ext_arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S,float *pSrc,float *pDst,uint32_t blockSize) __attribute__((noinline, section(".arm_fir_decimate_f32_sec")));

extern void setup_biquad_filter(float sampling_rate, float freq_low, float freq_high,
    void *flt_S, int param_5) __attribute__((noinline, section(".setup_biquad_filter_sec")));

#endif // __EXTERNAL_H
