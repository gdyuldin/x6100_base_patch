#ifndef __EXTERNAL_H
#define __EXTERNAL_H

#include "stdint.h"

#define M_PI_F 3.14159265358979323846f
#define M_TWOPI_F 6.283185307179586f

// /**
//  * @brief Instance structure for the floating-point Biquad cascade filter.
//  */
// typedef struct
// {
//     uint32_t numStages;   /**< number of 2nd order stages in the filter.  Overall order is 2*numStages. */

//     // {x[n-1], x[n-2], y[n-1], y[n-2]}
//     float *pState;        /**< Points to the array of state coefficients.  The array is of length 4*numStages. */

//     // {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22, ...}
//     float *pCoeffs; /**< Points to the array of coefficients.  The array is of length 5*numStages. */
// } arm_biquad_casd_df1_inst_f32;

// /**
//   @brief Instance structure for single precision floating-point FIR decimator.
//  */
// typedef struct
// {
//     uint8_t M;            /**< decimation factor. */
//     uint16_t numTaps;     /**< number of coefficients in the filter. */
//     const float *pCoeffs; /**< points to the coefficient array. The array is of length numTaps.*/
//     float *pState;        /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
// } arm_fir_decimate_instance_f32;

// extern void arm_biquad_cascade_df1_f32 (arm_biquad_casd_df1_inst_f32 *S,float *pSrc,float *pDst, uint32_t blockSize)
//     __attribute__((noinline, section(".arm_biquad_cascade_df1_f32_sec")));

// extern void arm_fill_f32(float val, float *data, uint32_t size)
//     __attribute__((noinline, section(".arm_fill_f32_sec")));

// extern void arm_copy_f32(float *pSrc, float *pDst, uint32_t blockSize)
//     __attribute__((noinline, section(".arm_copy_f32_sec")));

extern float arm_sin_f32(float val)
    __attribute__((noinline, section(".arm_sin_f32_sec")));

extern float arm_cos_f32(float val)
__attribute__((noinline, section(".arm_cos_f32_sec")));

extern void setup_biquad_filter(float sampling_rate, float freq_low, float freq_high,
    void *flt_S, int param_5) __attribute__((noinline, section(".setup_biquad_filter_sec")));
    // extern void arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S,float *pSrc,float *pDst,uint32_t blockSize) __attribute__((noinline, section(".arm_fir_decimate_f32_sec")));

extern float sqrt_f32(float val) __attribute__((noinline, section(".arm_sqrt_f32_sec")));

#endif // __EXTERNAL_H
