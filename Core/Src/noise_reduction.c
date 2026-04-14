#include "noise_reduction.h"
#include "offsets.h"
#include "external.h"
#include "utils.h"

#include <stdbool.h>

#define ARM_RFFT_INIT_HELPER(n) arm_rfft_fast_init_ ##n## _f32
#define ARM_RFFT_INIT(x) ARM_RFFT_INIT_HELPER(x)
#define CCMRAM __attribute((section(".ccmram")))

#define CLIP(x, low, high) (x > high ? high : (x < low ? low : x))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#define ABS(x) (x < 0 ? -x: x)

static void roll_left(float *pSrc, size_t steps, size_t srcSize);

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

const float gate_convolve_kernel[] = {
    6.250000e-02f,	1.250000e-01f,	1.875000e-01f,	2.500000e-01f,	1.875000e-01f,	1.250000e-01f,	6.250000e-02f,
};

typedef struct
{

    uint32_t profile_update_delay;

    // FFT history
    float fft_hist[NR_NFFT];

    // AGC history
    float agc_scales[NR_NFFT];
    float *agc_scale_write;

    // input buffer
    float in_buf[NR_NFFT];
    uint16_t in_buf_i;

    // FFT
    arm_rfft_fast_instance_f32 rfft;

    // Noise profile LPF parameters
    float profile_fall_alpha;
    float profile_grow_alpha;
    float profile[NR_MASK_SIZE];

    // gate gain
    float gate_gain_inc_alpha;
    float gate_gain_dec_alpha;
    float gate_gain[NR_MASK_SIZE];
    float slope;

    // Output buffer
    float out_buf[NR_NFFT];

    // Filters
    uint32_t filter_low_bin;
    uint32_t filter_high_bin;
} nr_data_t;

CCMRAM nr_data_t nr;

CCMRAM float buf1[NR_NFFT];
CCMRAM float buf2[NR_NFFT];
CCMRAM float mag[NR_MASK_SIZE];
CCMRAM float gain_db[NR_MASK_SIZE + ARRAY_SIZE(gate_convolve_kernel) - 1];

int nr_init(void)
{
    ARM_RFFT_INIT(NR_NFFT)(&nr.rfft);

    // Time in seconds
    nr.profile_grow_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 5.0f));
    nr.profile_fall_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 0.1f));

    // 1 - np.exp(-self.hop / (self.sr * 0.05))
    // Time in seconds
    nr.gate_gain_inc_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 0.01f));
    nr.gate_gain_dec_alpha = 1.0f - expf(-(float)NR_HOP / (NR_SAMPLING_RATE * 0.05f));

    nr.slope = 3.0f;

    nr_reset();
    nr.profile_update_delay = 0;

    return 0;
}

void nr_set_slope(uint8_t slope) {
    if (slope > 1) {
        nr.slope = slope;
    }
}

void nr_reset(void) {
    nr.in_buf_i = 0;
    ext_arm_fill_f32(0.0f, nr.fft_hist, NR_NFFT);
    ext_arm_fill_f32(0.0f, nr.out_buf, NR_NFFT);
    ext_arm_fill_f32(0.0f, nr.gate_gain, NR_MASK_SIZE);
    ext_arm_fill_f32(-40.0f, nr.profile, NR_MASK_SIZE);

    ext_arm_fill_f32(100.0f, nr.agc_scales, ARRAY_SIZE(nr.agc_scales));
    nr.agc_scale_write = nr.agc_scales;
}

void nr_setup_filters(void) {
    USE_OEM_MODULATION_AS(pMode);

    struct filter_freqs_t {
        int32_t low;
        int32_t high;
    };

    struct filter_freqs_t *filter_frequencies = (struct filter_freqs_t *)FILTER_FREQUENCIES;

    uint32_t low1 = ABS(filter_frequencies[0].low);
    uint32_t low2 = ABS(filter_frequencies[1].low);
    uint32_t high1 = ABS(filter_frequencies[0].high);
    uint32_t high2 = ABS(filter_frequencies[1].high);
    low1 = MIN(low1, low2);
    high1 = MAX(high1, high2);

    if ((*pMode == MOD_AM) || (*pMode == MOD_NFM)) {
        low1 = 50;
    };

    int32_t low_bin = low1 * NR_NFFT / NR_SAMPLING_RATE - 2;
    int32_t high_bin = high1 * NR_NFFT / NR_SAMPLING_RATE + 2;

    nr.filter_low_bin = MAX(low_bin, 0);
    nr.filter_high_bin = MIN(high_bin, NR_MASK_SIZE - 1);
    nr.filter_high_bin = MAX(nr.filter_high_bin, nr.filter_low_bin + 2);
}

void nr_apply(float sample)
{
    USE_OEM_NRE_AS(nre_flag);
    USE_OEM_MODULATION_AS(pMode);

    float *nrthr = (float *)NR_THR_F;
    uint32_t *nr_out_write = (uint32_t *)NR_OUT_WRITE;
    uint32_t *nr_out_read = (uint32_t *)NR_OUT_READ;
    float *nr_out_buf = (float *)NR_OUT_BUF;

    float *agc_scale = (float*)AGC_SCALE;
    uint8_t *agc_on = (uint8_t*)AGC_ON;

    if (*nre_flag == 0) {
        *nr_out_write = 0;
        *nr_out_read = 0;

        nr.agc_scale_write = nr.agc_scales;
        nr.in_buf_i = 0;
        return;
    }

    if (*pMode == MOD_NFM) {
        // Just copy data without NFM
        nr_out_buf[*nr_out_write] = sample;
        *nr_out_write = (*nr_out_write + 1) & 0x1ff;
        return;
    }

    if (!*agc_on) {
        *nr.agc_scale_write++ = 100.0f;
    } else {
        *nr.agc_scale_write++ = *agc_scale;
    }

    if ((nr.agc_scales + ARRAY_SIZE(nr.agc_scales)) <= nr.agc_scale_write) {
        nr.agc_scale_write = nr.agc_scales;
    }

    // Copy data to buf
    if (isnan(sample)) {
        sample = 0.0f;
    } else {
        sample = CLIP(sample, -2.0f, 2.0f);
    }
    nr.in_buf[nr.in_buf_i] = sample;
    nr.in_buf_i++;

    if (nr.in_buf_i >= NR_NFFT) {

        /* Compute AGC k */
        float agc_k = 0.0f;
        for (size_t i = 0; i < ARRAY_SIZE(nr.agc_scales); i++)
        {
            agc_k += nr.agc_scales[i] * nr.agc_scales[i];
        }

        agc_k = 100.0f * ARRAY_SIZE(nr.agc_scales) / ext_sqrt_f32(agc_k);

        /* Apply window */
        float *in_chunk = buf1;
        arm_mult_f32(nr.in_buf, window, in_chunk, NR_NFFT);
        roll_left(nr.in_buf, NR_HOP, NR_NFFT);
        nr.in_buf_i = NR_NFFT - NR_HOP;

        /* FFT */
        float *z = buf2;
        arm_rfft_fast_f32(&nr.rfft, in_chunk, z, 0);

        /* Compute magnitude (only for filtered fft bins) */
        ext_arm_fill_f32(0.0f, mag, NR_MASK_SIZE);
        uint32_t n_bins = nr.filter_high_bin - nr.filter_low_bin;
        arm_cmplx_mag_f32(z + 2 + nr.filter_low_bin * 2, mag + nr.filter_low_bin, n_bins);

        #pragma GCC unroll 4
        for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++)
        {
            // Revert AGC, convert to db
            mag[i] = lin2db(mag[i] * agc_k);
        }

        if (nr.profile_update_delay != 0) {
            nr.profile_update_delay--;
        } else {
            /* Compute noise profile */
#pragma GCC unroll 4
            for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++)
            {
                float diff = mag[i] - nr.profile[i];
                if (diff > 0) {
                    nr.profile[i] += diff * nr.profile_grow_alpha;
                } else {
                    nr.profile[i] += diff * nr.profile_fall_alpha;
                }
            }
        }

        /* Compute SNR */
        float th = *nrthr / 8.0f + 12.5f;
        for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++)
        {
            float snr = mag[i] - nr.profile[i];
            snr = (snr - th) * nr.slope;
            snr = CLIP(snr, -40.0f, 0.0f);
            float diff = snr - nr.gate_gain[i];
            if (diff > 0) {
                // Attack
                nr.gate_gain[i] += diff * nr.gate_gain_inc_alpha;
            } else {
                // Release
                nr.gate_gain[i] += diff * nr.gate_gain_dec_alpha;
            }
        }
        /* Convolve gain */
        ext_arm_fill_f32(0.0f, gain_db, ARRAY_SIZE(gain_db));
        arm_conv_f32(
            nr.gate_gain + nr.filter_low_bin, n_bins,
            gate_convolve_kernel, ARRAY_SIZE(gate_convolve_kernel),
            gain_db + nr.filter_low_bin);
        float *gain_aligned = gain_db + (ARRAY_SIZE(gate_convolve_kernel) - 1) / 2;

        /* Gain to lin */
        for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++)
        {
            gain_aligned[i] = db2lin(gain_aligned[i]);
        }

        /* Apply gate */
        float *z_filtered = buf1;
        ext_arm_fill_f32(0.0f, buf1, ARRAY_SIZE(buf1));
        arm_cmplx_mult_real_f32(
            nr.fft_hist + 2 + nr.filter_low_bin * 2,
            gain_aligned + nr.filter_low_bin,
            z_filtered + 2 + nr.filter_low_bin * 2, n_bins);

        /* Copy fft to history */
        ext_arm_copy_f32(z, nr.fft_hist, NR_NFFT);

        /* IFFT */
        float *r_samples = buf2;
        arm_rfft_fast_f32(&nr.rfft, z_filtered, r_samples, 1);

        /* Window and sum */
#pragma GCC unroll 4
        for (size_t i = 0; i < NR_NFFT; i++)
        {
            nr.out_buf[i] += r_samples[i] * window[i];
        }

        /* Copy output to buf */
        uint32_t write = *nr_out_write;
        for (size_t i = 0; i < NR_HOP; i++)
        {
            nr_out_buf[write] = nr.out_buf[i] * NR_NORM_VAL;
            write = (write + 1) & 0x1ff;
        }
        *nr_out_write = write;

        /* Roll output buffer */
        roll_left(nr.out_buf, NR_HOP, NR_NFFT);
        ext_arm_fill_f32(0.0f, nr.out_buf + NR_NFFT - NR_HOP, NR_HOP);
    }
}

void nr_pause_update()
{
    nr.profile_update_delay = 12500 / 64;
}

static void roll_left(float *pSrc, size_t steps, size_t srcSize)
{
    float *dst = pSrc;
    float *src = dst + steps;
    float *stop = pSrc + srcSize;
    while (src < stop)
    {
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
    }
}

