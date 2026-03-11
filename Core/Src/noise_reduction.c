#include "noise_reduction.h"
#include "offsets.h"

#define ARM_RFFT_INIT_HELPER(n) arm_rfft_fast_init_ ##n## _f32
#define ARM_RFFT_INIT(x) ARM_RFFT_INIT_HELPER(x)

#define CLIP(a, l, h) (MAX(MIN(a, h), l))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#define ABS(x) (x < 0 ? -x: x)

static inline void roll_left(float *pSrc, size_t steps, size_t srcSize);
static inline void lpr_noise_profile(float *mag, size_t len);

nr_data_t nr_data __attribute((section(".ccmram")));

const float window[NR_NFFT] = {
    0.000000e+00f,	1.505907e-04f,	6.022719e-04f,	1.354772e-03f,	2.407637e-03f,	3.760233e-03f,	5.411745e-03f,	7.361179e-03f,
    9.607360e-03f,	1.214893e-02f,	1.498437e-02f,	1.811197e-02f,	2.152983e-02f,	2.523591e-02f,	2.922797e-02f,	3.350360e-02f,
    3.806023e-02f,	4.289512e-02f,	4.800535e-02f,	5.338785e-02f,	5.903937e-02f,	6.495650e-02f,	7.113569e-02f,	7.757322e-02f,
    8.426519e-02f,	9.120759e-02f,	9.839623e-02f,	1.058268e-01f,	1.134948e-01f,	1.213956e-01f,	1.295244e-01f,	1.378765e-01f,
    1.464466e-01f,	1.552297e-01f,	1.642205e-01f,	1.734136e-01f,	1.828034e-01f,	1.923842e-01f,	2.021503e-01f,	2.120959e-01f,
    2.222149e-01f,	2.325012e-01f,	2.429486e-01f,	2.535509e-01f,	2.643016e-01f,	2.751943e-01f,	2.862225e-01f,	2.973793e-01f,
    3.086583e-01f,	3.200525e-01f,	3.315551e-01f,	3.431591e-01f,	3.548577e-01f,	3.666436e-01f,	3.785099e-01f,	3.904494e-01f,
    4.024548e-01f,	4.145191e-01f,	4.266348e-01f,	4.387947e-01f,	4.509914e-01f,	4.632177e-01f,	4.754662e-01f,	4.877294e-01f,
    5.000000e-01f,	5.122706e-01f,	5.245338e-01f,	5.367823e-01f,	5.490086e-01f,	5.612053e-01f,	5.733652e-01f,	5.854809e-01f,
    5.975452e-01f,	6.095506e-01f,	6.214901e-01f,	6.333564e-01f,	6.451423e-01f,	6.568409e-01f,	6.684449e-01f,	6.799475e-01f,
    6.913417e-01f,	7.026207e-01f,	7.137775e-01f,	7.248057e-01f,	7.356984e-01f,	7.464491e-01f,	7.570514e-01f,	7.674988e-01f,
    7.777851e-01f,	7.879041e-01f,	7.978497e-01f,	8.076158e-01f,	8.171966e-01f,	8.265864e-01f,	8.357795e-01f,	8.447703e-01f,
    8.535534e-01f,	8.621235e-01f,	8.704756e-01f,	8.786044e-01f,	8.865052e-01f,	8.941732e-01f,	9.016038e-01f,	9.087924e-01f,
    9.157348e-01f,	9.224268e-01f,	9.288643e-01f,	9.350435e-01f,	9.409606e-01f,	9.466122e-01f,	9.519946e-01f,	9.571049e-01f,
    9.619398e-01f,	9.664964e-01f,	9.707720e-01f,	9.747641e-01f,	9.784702e-01f,	9.818880e-01f,	9.850156e-01f,	9.878511e-01f,
    9.903926e-01f,	9.926388e-01f,	9.945883e-01f,	9.962398e-01f,	9.975924e-01f,	9.986452e-01f,	9.993977e-01f,	9.998494e-01f,
    1.000000e+00f,	9.998494e-01f,	9.993977e-01f,	9.986452e-01f,	9.975924e-01f,	9.962398e-01f,	9.945883e-01f,	9.926388e-01f,
    9.903926e-01f,	9.878511e-01f,	9.850156e-01f,	9.818880e-01f,	9.784702e-01f,	9.747641e-01f,	9.707720e-01f,	9.664964e-01f,
    9.619398e-01f,	9.571049e-01f,	9.519946e-01f,	9.466122e-01f,	9.409606e-01f,	9.350435e-01f,	9.288643e-01f,	9.224268e-01f,
    9.157348e-01f,	9.087924e-01f,	9.016038e-01f,	8.941732e-01f,	8.865052e-01f,	8.786044e-01f,	8.704756e-01f,	8.621235e-01f,
    8.535534e-01f,	8.447703e-01f,	8.357795e-01f,	8.265864e-01f,	8.171966e-01f,	8.076158e-01f,	7.978497e-01f,	7.879041e-01f,
    7.777851e-01f,	7.674988e-01f,	7.570514e-01f,	7.464491e-01f,	7.356984e-01f,	7.248057e-01f,	7.137775e-01f,	7.026207e-01f,
    6.913417e-01f,	6.799475e-01f,	6.684449e-01f,	6.568409e-01f,	6.451423e-01f,	6.333564e-01f,	6.214901e-01f,	6.095506e-01f,
    5.975452e-01f,	5.854809e-01f,	5.733652e-01f,	5.612053e-01f,	5.490086e-01f,	5.367823e-01f,	5.245338e-01f,	5.122706e-01f,
    5.000000e-01f,	4.877294e-01f,	4.754662e-01f,	4.632177e-01f,	4.509914e-01f,	4.387947e-01f,	4.266348e-01f,	4.145191e-01f,
    4.024548e-01f,	3.904494e-01f,	3.785099e-01f,	3.666436e-01f,	3.548577e-01f,	3.431591e-01f,	3.315551e-01f,	3.200525e-01f,
    3.086583e-01f,	2.973793e-01f,	2.862225e-01f,	2.751943e-01f,	2.643016e-01f,	2.535509e-01f,	2.429486e-01f,	2.325012e-01f,
    2.222149e-01f,	2.120959e-01f,	2.021503e-01f,	1.923842e-01f,	1.828034e-01f,	1.734136e-01f,	1.642205e-01f,	1.552297e-01f,
    1.464466e-01f,	1.378765e-01f,	1.295244e-01f,	1.213956e-01f,	1.134948e-01f,	1.058268e-01f,	9.839623e-02f,	9.120759e-02f,
    8.426519e-02f,	7.757322e-02f,	7.113569e-02f,	6.495650e-02f,	5.903937e-02f,	5.338785e-02f,	4.800535e-02f,	4.289512e-02f,
    3.806023e-02f,	3.350360e-02f,	2.922797e-02f,	2.523591e-02f,	2.152983e-02f,	1.811197e-02f,	1.498437e-02f,	1.214893e-02f,
    9.607360e-03f,	7.361179e-03f,	5.411745e-03f,	3.760233e-03f,	2.407637e-03f,	1.354772e-03f,	6.022719e-04f,	1.505907e-04f,
};

const float mask_convolve_kernel[] = {
    3.225806e-02f,	9.677419e-02f,	1.612903e-01f,	2.258065e-01f,	2.258065e-01f,	1.612903e-01f,	9.677419e-02f,
};

int nr_init(void)
{
    ARM_RFFT_INIT(NR_NFFT)(&nr_data.rfft);
    ARM_RFFT_INIT(NR_NFFT)(&nr_data.rifft);

    // self.increase_a = 1 - np.exp(-self.hop / (self.sr * 5))
    // self.decrease_a = 1 - np.exp(-self.hop / (self.sr * 0.5))
    // Time in seconds
    nr_data.profile_grow_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 5.0f));
    nr_data.profile_fall_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 0.5f));

    // 1 - np.exp(-self.hop / (self.sr * 0.05))
    // Time in seconds
    nr_data.mask_avg_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 0.05f));

    // Init norm
    arm_fill_f32(0.0f, nr_data.norm, NR_HOP);
    for (size_t i = 0; i < NR_NFFT; i++)
    {
        nr_data.norm[i % NR_HOP] += window[i] * window[i];
    }
    for (size_t i = 0; i < NR_HOP; i++)
    {
        nr_data.norm[i] = 1.0f / nr_data.norm[i];
    }

    nr_data.slope = 10;
    nr_data.in_buf_i = 0;
    arm_fill_f32(0.0f, nr_data.out_buf, NR_NFFT);
    arm_fill_f32(0.0f, nr_data.mask_avg, NR_MASK_SIZE);
    arm_fill_f32(1.0f, nr_data.profile, NR_MASK_SIZE);

    for (size_t i = 0; i < NR_MASK_SIZE; i++)
    {
        nr_data.freq_corr[i] = 1.0f + (float)i / NR_MASK_SIZE;
    }

    return 0;
}

void nr_setup_filters(void) {

    struct filter_freqs_t {
        int32_t low;
        int32_t high;
    };

    struct filter_freqs_t *filter_frequencies = (struct filter_freqs_t *)FILTER_FREQUENCIES;

    uint32_t low1 = ABS(filter_frequencies[0].low);
    uint32_t low2 = ABS(filter_frequencies[1].low);
    uint32_t high1 = ABS(filter_frequencies[0].high);
    uint32_t high2 = ABS(filter_frequencies[1].high);
    low1 = MAX(low1, low2);
    high1 = MIN(high1, high2);

    int32_t low_bin = low1 * NR_NFFT / NR_SAMPLING_RATE - 2;
    int32_t high_bin = high1 * NR_NFFT / NR_SAMPLING_RATE + 2;

    nr_data.filter_low_bin = MAX(low_bin, 1);
    nr_data.filter_high_bin = MIN(high_bin, NR_MASK_SIZE - 1);
}

int nr_apply(float sample)
{
    uint8_t *nre_flag = (uint8_t *)NRE_FLAG;
    float *nrthr = (float *)NR_THR_F;
    uint32_t *nr_out_write = (uint32_t *)NR_OUT_WRITE;
    float *nr_out_buf = (float *)NR_OUT_BUF;

    if (*nre_flag == 0) {
        return 0;
    }

    // Copy data to buf
    nr_data.in_buf[nr_data.in_buf_i] = sample;
    nr_data.in_buf_i++;

    float buf1[NR_NFFT];
    float buf2[NR_NFFT];

    if (nr_data.in_buf_i >= NR_NFFT) {

        // Apply window
        float *in_chunk = buf1;
        arm_mult_f32(nr_data.in_buf, window, in_chunk, NR_NFFT);
        roll_left(nr_data.in_buf, NR_HOP, NR_NFFT);
        nr_data.in_buf_i = NR_NFFT - NR_HOP;

        // FFT
        float *z = buf2;
        arm_rfft_fast_f32(&nr_data.rfft, in_chunk, z, 0);

        // Compute magnitude
        float mag[NR_MASK_SIZE];
        arm_cmplx_mag_f32(z + 2, mag, NR_MASK_SIZE);

        // Compute mask
        float mask[NR_MASK_SIZE];
        float offset = *nrthr / 10.0f;
        for (size_t i = 0; i < NR_MASK_SIZE; i++)
        {
            // np.clip((mag / self.thresholds - 1 + 3 - offset) * slope / 6, 0, 1)
            float val = ((mag[i] / nr_data.profile[i] + 0.5f - offset) * nr_data.slope * nr_data.freq_corr[i]) / 6;
            mask[i] = CLIP(val, 0, 1);
            nr_data.mask_avg[i] += (mask[i] - nr_data.mask_avg[i]) * nr_data.mask_avg_alpha;
        }

        // Convolve mask
        float conv_mask[NR_MASK_SIZE + ARRAY_SIZE(mask_convolve_kernel) - 1];
        arm_conv_f32(nr_data.mask_avg, NR_MASK_SIZE, mask_convolve_kernel, ARRAY_SIZE(mask_convolve_kernel), conv_mask);
        float *conv_mask_aligned = conv_mask + (ARRAY_SIZE(mask_convolve_kernel) - 1) / 2;

        // Zero outside filters
        arm_fill_f32(0.0f, conv_mask_aligned, nr_data.filter_low_bin);
        arm_fill_f32(0.0f, conv_mask_aligned + nr_data.filter_high_bin, NR_MASK_SIZE - nr_data.filter_high_bin);

        float *z_filtered = buf1;
        // arm_copy_f32(z, z_filtered, NR_NFFT);
        arm_cmplx_mult_real_f32(z + 2, conv_mask_aligned, z_filtered + 2, NR_MASK_SIZE);
        // Set 0 for first and last
        z_filtered[0] = 0;
        z_filtered[1] = 0;

        // IFFT
        float *out_chunk = buf2;
        arm_rfft_fast_f32(&nr_data.rifft, z_filtered, out_chunk, 1);

        // Window, sum and norm
        arm_mult_f32(out_chunk, window, out_chunk, NR_NFFT);
        arm_add_f32(out_chunk, nr_data.out_buf, nr_data.out_buf, NR_NFFT);
        arm_mult_f32(nr_data.out_buf, nr_data.norm, nr_data.out_buf, NR_HOP);

        // Copy output to buf
        uint32_t write = *nr_out_write;
        for (size_t i = 0; i < NR_HOP; i++)
        {
            nr_out_buf[write] = nr_data.out_buf[i];
            write = (write + 1) & 0x1ff;
        }
        *nr_out_write = write;

        // Roll output buffer
        roll_left(nr_data.out_buf, NR_HOP, NR_NFFT);
        arm_fill_f32(0.0f, nr_data.out_buf + NR_NFFT - NR_HOP, NR_HOP);
    }
    return 0;
}

inline void roll_left(float *pSrc, size_t steps, size_t srcSize)
{
    float *dst = pSrc + srcSize - steps - 1;
    float *src = dst + steps;
    do
    {
        *dst-- = *src--;
        *dst-- = *src--;
        *dst-- = *src--;
        *dst-- = *src--;
    } while (dst > pSrc);

}

inline void lpr_noise_profile(float *mag, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        float diff = mag[i] - nr_data.profile[i];
        if (diff > 0) {
            nr_data.profile[i] += diff * nr_data.profile_grow_alpha;
        } else {
            nr_data.profile[i] += diff * nr_data.profile_fall_alpha;
        }
    }

}
