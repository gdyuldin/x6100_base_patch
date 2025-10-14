#ifndef __COMPRESSOR_H
#define __COMPRESSOR_H

#include "stdint.h"

#include "stm32f4xx_hal.h"

// from CMSIS-DSP Include/dsp/filtering_functions.h
/**
  @brief Instance structure for single precision floating-point FIR decimator.
 */
typedef struct
{
    uint8_t M;            /**< decimation factor. */
    uint16_t numTaps;     /**< number of coefficients in the filter. */
    const float *pCoeffs; /**< points to the coefficient array. The array is of length numTaps.*/
    float *pState;        /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
} arm_fir_decimate_instance_f32;

extern float compress(float val);
extern void init_data(void);
extern void configure();
extern void apply_rx_iq_offset(void);
extern float am_fm_rx_process(float val, float *i, float *q, uint8_t modulation);
extern void tx_amp(float *i, float *q);
extern void tx_coeff_calc(float pwr);
extern void anf_update();
extern uint32_t copy_flow_samples_to_arg(float *p_Dst);

// extern void dump_boot(void);

#endif // __COMPRESSOR_H
