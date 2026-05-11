#ifndef __NOISE_REDUCTION_H
#define __NOISE_REDUCTION_H

#include "stdint.h"
#include "stddef.h"

#include <dsp/transform_functions.h>
#include <dsp/filtering_functions.h>


int nr_init(void);
void nr_reset(void);
void nr_setup_filters(uint32_t low1, uint32_t low2, uint32_t high1, uint32_t high2);
void nr_setup_threshold(float value);

void nr_prepare(void);
float nr_apply(float sample);

void nr_pause_update();

#endif // __NOISE_REDUCTION_H
