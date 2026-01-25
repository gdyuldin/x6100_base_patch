#ifndef __COMPRESSOR_H
#define __COMPRESSOR_H

#include "stdint.h"

#include "stm32f4xx_hal.h"

// from CMSIS-DSP Include/dsp/filtering_functions.h


typedef struct {
    float real;
    float imag;
} cfloat_t;

extern void compress(float *val);
extern float am_modulation(float val, float am_carrier_lvl, float am_level);
extern float fm_preemphasis(float val);
extern void init_data(void);
extern void configure(void);
extern void dma_end(void);
extern void apply_rx_iq_offset(void);
extern void if_shift(void);
extern int32_t tx_if_shift(int32_t lo_freq_shift);
extern void fm_demodulate(void *_ignore, cfloat_t *iq_sample, float *out, uint32_t _n_samples);
extern float fm_modulate(float val);
extern void am_fm_rx_process();
extern void tx_amp(float *i, float *q);
extern void tx_coeff_calc(float pwr);
extern void anf_update(void);
extern void process_i2c_cmd(void);
extern uint32_t copy_flow(float *p_Dst);

// extern void dump_boot(void);

#endif // __COMPRESSOR_H
