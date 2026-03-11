#ifndef __NOISE_REDUCTION_H
#define __NOISE_REDUCTION_H

#include "stdint.h"
#include "stddef.h"

#include <dsp/transform_functions.h>
#include <dsp/filtering_functions.h>

#define NRE 0

#define NR_SAMPLING_RATE 12500
#define NR_NFFT 256
#define NR_HOP (NR_NFFT / 2)
#define NR_MASK_SIZE (NR_NFFT / 2 - 1)

typedef struct
{
    // input buffer
    float in_buf[NR_NFFT];
    uint16_t in_buf_i;

    // FFT
    arm_rfft_fast_instance_f32 rfft;
    arm_rfft_fast_instance_f32 rifft;

    // Noise profile LPF parameters
    float profile_fall_alpha;
    float profile_grow_alpha;
    float profile[NR_MASK_SIZE];

    // Mask
    float mask_avg_alpha;
    float mask_avg[NR_MASK_SIZE];
    float slope;

    // High freq correction
    float freq_corr[NR_MASK_SIZE];

    // Output buffer
    float norm[NR_HOP];
    float out_buf[NR_NFFT];

    // Filters
    uint32_t filter_low_bin;
    uint32_t filter_high_bin;
} nr_data_t;

int nr_init(void);
void nr_setup_filters(void);
int nr_apply(float sample);

#endif // __NOISE_REDUCTION_H
