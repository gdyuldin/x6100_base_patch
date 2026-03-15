#ifndef __MODULATIONS_H
#define __MODULATIONS_H

#include "utils.h"
#include "stdint.h"
#include "stdbool.h"

void fm_demod_init(void);

void fm_demodulate(void *S, cfloat_t *iq_sample, float *out, uint32_t _n_samples);

float am_modulation(float val, float am_carrier_lvl, float am_level);

float fm_modulate(float val);

void set_fm_demod_emphasis(bool on);

#endif //__MODULATIONS_H
