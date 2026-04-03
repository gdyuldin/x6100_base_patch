#ifndef __NOISE_REDUCTION_H
#define __NOISE_REDUCTION_H

#include "stdint.h"
#include "stddef.h"

#include <dsp/transform_functions.h>
#include <dsp/filtering_functions.h>

#define NRE 0

#define NR_SAMPLING_RATE 12500
#define NR_NFFT 256
#define NR_HOP 64

#define NR_NORM_VAL 0.6666666666666666f  // Constant for hann window and  1/4 hop
// math.lcm(NR_NFFT, NR_HOP)
// #define NR_NORM_LEN 256
#define NR_MASK_SIZE (NR_NFFT / 2 - 1)


int nr_init(void);
void nr_reset(void);
void nr_setup_filters(void);
void nr_apply(float sample);

void nr_pause_update();

#endif // __NOISE_REDUCTION_H
