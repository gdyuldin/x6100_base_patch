#ifndef __COMPRESSOR_H
#define __COMPRESSOR_H

#include "stdint.h"

#include "stm32f4xx_hal.h"

extern float compress(float val);
extern void init_data(void);
extern void configure();
extern void apply_rx_iq_offset(void);
extern float am_fm_rx_process(float val, float *i, float *q, uint8_t modulation);
extern void tx_amp(float *i, float *q);
extern void tx_coeff_calc(float pwr);
extern void anf_update();

// extern void dump_boot(void);

#endif // __COMPRESSOR_H
