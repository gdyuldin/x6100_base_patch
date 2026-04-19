#include "noise_reduction.h"
#include "offsets.h"
#include "external.h"
#include "utils.h"

#include "noise_reduction_data.c"

#include <stdbool.h>
#include <arm_math.h>

#define NR_SAMPLING_RATE 12500
#define NR_MAX_NFFT 512

#define NR_NORM_VAL 0.6666666666666666f  // Constant for hann window and  1/4 hop
#define NR_MAX_MASK_SIZE (NR_MAX_NFFT / 2 - 1)

// For some reasons, it doesn't working with variables
// nr.noise_psd_alpha_up = 1.0f - expf(-(float)nr.fft->hop / (NR_SAMPLING_RATE * 5.0f));
// nr.noise_psd_alpha_down = 1.0f - expf(-(float)nr.fft->hop / (NR_SAMPLING_RATE * 0.1f));
// Values is optimal for 512 NFFT, a bit fast for 256
#define NR_NOISE_ALPHA_UP 0.0020459042789229276f
#define NR_NOISE_ALPHA_DOWN 0.09733158791905794f

typedef struct {
    float data[NR_MAX_NFFT];
    uint32_t cap;
    uint32_t w;
    uint32_t r;
} buf_t;

typedef struct {
    // FFT
    arm_rfft_fast_instance_f32 rfft;
    uint32_t N;
    uint32_t hop;
    uint32_t overlap;
    uint32_t mask_size;
} nr_fft_t;

enum step_t {
    STAGE1,
    STAGE2,
};


typedef struct
{
    uint32_t profile_update_delay;

    float prev_mag[NR_MAX_MASK_SIZE];

    // Noise PSD
    float noise_psd[NR_MAX_MASK_SIZE];

    // Smooth gain to apply
    float gain_smooth[NR_MAX_MASK_SIZE];

    // AGC history
    buf_t agc_scales;

    // input buffer
    buf_t in_buf;

    // Output buffer
    buf_t out_buf;

    uint32_t buf_mask;

    nr_fft_t fft_512_S;
    nr_fft_t fft_256_S;
    nr_fft_t *fft;

    // Filters
    uint32_t filter_low_bin;
    uint32_t filter_high_bin;

    // Parameters
    float alpha;
    float beta;
    float lambda_g;

    // Soft clip parameters
    float ptp;
    float ptp_inv;

    // State
    enum step_t cur_step;
    enum step_t next_step;

    // Buffers
    float Z[NR_MAX_NFFT];
    float signal_buf[NR_MAX_NFFT];
} nr_data_t;


static void nr_set_fft_size(uint16_t size);

__STATIC_FORCEINLINE void update_mag_mag2(float *z, int i, float agc_k2);
__STATIC_FORCEINLINE float soft_clip(float x, const float min, const float ptp, const float ptp_inv);

__STATIC_FORCEINLINE void nr_buf_reset(buf_t *buf);
__STATIC_FORCEINLINE void nr_buf_put(buf_t *buf, float val);
// Add a value to already stored and move write pointer
__STATIC_FORCEINLINE void nr_buf_add(buf_t *buf, float val);
__STATIC_FORCEINLINE float nr_buf_get(buf_t *buf);
__STATIC_FORCEINLINE float nr_buf_get_set(buf_t *buf, float new_val);
__STATIC_FORCEINLINE uint32_t nr_buf_ready_size(buf_t *buf);
// Left shift read
__STATIC_FORCEINLINE void nr_buf_lsr(buf_t *buf, uint32_t shift);
// Left shift write
__STATIC_FORCEINLINE void nr_buf_lsw(buf_t *buf, uint32_t shift);

// Split processing to fit performance
__STATIC_FORCEINLINE void step1();
__STATIC_FORCEINLINE void step2();


CCMRAM nr_data_t nr;

CCMRAM float mag2[NR_MAX_MASK_SIZE];
CCMRAM float mag2_avg[NR_MAX_MASK_SIZE];
CCMRAM float raw_gains[NR_MAX_MASK_SIZE];

CCMRAM float window[NR_MAX_NFFT];


int nr_init(void)
{
    // Init fft 512
    arm_rfft_fast_init_512_f32(&nr.fft_512_S.rfft);
    nr.fft_512_S.N = 512;
    nr.fft_512_S.hop = 128;
    nr.fft_512_S.overlap = nr.fft_512_S.N - nr.fft_512_S.hop;
    nr.fft_512_S.mask_size = 255;

    // Init fft 256
    arm_rfft_fast_init_256_f32(&nr.fft_256_S.rfft);
    nr.fft_256_S.N = 256;
    nr.fft_256_S.hop = 64;
    nr.fft_256_S.overlap = nr.fft_256_S.N - nr.fft_256_S.hop;
    nr.fft_256_S.mask_size = 127;

    nr_set_fft_size(512);

    nr.buf_mask = ARRAY_SIZE(nr.in_buf.data) - 1;

    nr.agc_scales.cap = ARRAY_SIZE(nr.agc_scales.data);
    nr.in_buf.cap = ARRAY_SIZE(nr.in_buf.data);
    nr.out_buf.cap = ARRAY_SIZE(nr.out_buf.data);

    nr_reset();
    nr.profile_update_delay = 0;
    nr.cur_step = STAGE1;
    nr.next_step = STAGE1;

    return 0;
}


void nr_reset(void) {
    nr_buf_reset(&nr.in_buf);
    nr_buf_reset(&nr.out_buf);
    ext_arm_fill_f32(0.0f, nr.out_buf.data, ARRAY_SIZE(nr.out_buf.data));

    ext_arm_fill_f32(1.0f, nr.gain_smooth, NR_MAX_MASK_SIZE);
    ext_arm_fill_f32(0.0f, nr.prev_mag, NR_MAX_MASK_SIZE);
    ext_arm_fill_f32(0.1f, nr.noise_psd, NR_MAX_MASK_SIZE);

    nr_buf_reset(&nr.agc_scales);
    ext_arm_fill_f32(100.0f, nr.agc_scales.data, ARRAY_SIZE(nr.agc_scales.data));
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

    if ((high1 - low1) > 3200) {
        if (nr.fft->N != 256) {
            nr_set_fft_size(256);
            for (uint16_t i = 1; i < nr.fft->N - 1; i++)
            {
                nr.prev_mag[i] = (nr.prev_mag[i * 2] + nr.prev_mag[i * 2 + 1]) * 0.25f;  // 1 / (2 + 2)
                nr.noise_psd[i] = (nr.noise_psd[i * 2] + nr.noise_psd[i * 2 + 1]) * 0.125f;  // 1 / (4 + 4)
                nr.gain_smooth[i] = (nr.gain_smooth[i * 2] + nr.gain_smooth[i * 2 + 1]) * 0.5f;
            }
        }
    } else {
        if (nr.fft->N != 512) {
            nr_set_fft_size(512);
            for (uint16_t i = (nr.fft->N / 2) - 1; i > 0; i--)
            {
                float prev_mag = nr.prev_mag[i] * 2.0f;
                float noise_psd = nr.noise_psd[i] * 4.0f;
                float gain_smooth = nr.gain_smooth[i];
                nr.prev_mag[2*i] = prev_mag;
                nr.prev_mag[2*i + 1] = prev_mag;
                nr.noise_psd[2 * i] = noise_psd;
                nr.noise_psd[2 * i + 1] = noise_psd;
                nr.gain_smooth[2 * i] = gain_smooth;
                nr.gain_smooth[2 * i + 1] = gain_smooth;
            }
        }
    }

    int32_t low_bin = low1 * nr.fft->N / NR_SAMPLING_RATE - 2;
    int32_t high_bin = high1 * nr.fft->N / NR_SAMPLING_RATE + 2;

    nr.filter_low_bin = MAX(low_bin, 1);
    nr.filter_high_bin = MIN(high_bin, NR_MAX_MASK_SIZE - 1);
    nr.filter_high_bin = MAX(nr.filter_high_bin, nr.filter_low_bin + 2);

    // Setup parameters
    float *nrthr = (float *)NR_THR_F;
    float depth = *nrthr / 30.0f;
    nr.alpha = 1.2f + (depth * 4.0f);
    // float beta = 0.1f - (depth * 0.08f);
    // beta = MAX(beta, 0.01f);
    nr.beta = 0.15f / pow10f_c(depth);
    nr.lambda_g = 0.5f + (depth * 0.2f);
    nr.lambda_g = (1.0f - MIN(nr.lambda_g, 0.7f));

    // Soft clip parameters
    nr.ptp = 1.0f - nr.beta;
    nr.ptp_inv = 1.0f / nr.ptp;

    nr.cur_step = nr.next_step;
}

void nr_apply(float sample)
{
    USE_OEM_NRE_AS(nre_flag);
    USE_OEM_MODULATION_AS(pMode);

    uint32_t *nr_out_write = (uint32_t *)NR_OUT_WRITE;
    uint32_t *nr_out_read = (uint32_t *)NR_OUT_READ;
    float *nr_out_buf = (float *)NR_OUT_BUF;

    float *agc_scale = (float*)AGC_SCALE;
    uint8_t *agc_on = (uint8_t*)AGC_ON;

    if (*nre_flag == 0) {
        *nr_out_write = 0;
        *nr_out_read = 0;

        nr_buf_reset(&nr.agc_scales);
        nr_buf_reset(&nr.in_buf);
        nr_buf_reset(&nr.out_buf);
        return;
    }

    if (*pMode == MOD_NFM) {
        // Just copy data without NFM
        nr_out_buf[*nr_out_write] = sample;
        *nr_out_write = (*nr_out_write + 1) & 0x1ff;
        return;
    }

    float agc2;
    if (!*agc_on) {
        // *nr.agc_scale_write++ = 100.0f * 100.0f;
        agc2 = 100000.0f;
    } else {
        agc2 = *agc_scale * *agc_scale;
    }
    nr_buf_put(&nr.agc_scales, agc2);

    // Copy data to buf
    if (isnan(sample)) {
        sample = 0.0f;
    } else {
        sample = CLIP(sample, -2.0f, 2.0f);
    }
    nr_buf_put(&nr.in_buf, sample);

    if (nr.cur_step == STAGE1) {
        step1();
    } else {
        step2();
    }

}

void nr_pause_update()
{
    nr.profile_update_delay = 12500 / 64;
}

static void nr_set_fft_size(uint16_t size) {
    if (size == 512) {
        nr.fft = &nr.fft_512_S;
        ext_arm_copy_f32(hann_window_512, window, ARRAY_SIZE(hann_window_512));
        // 1 - np.exp(-128 / (12500 * 0.05))
        // nr.noise_psd_alpha_up = 0.005106915141017465f;
        // nr.noise_psd_alpha_down = 0.18518973783127057f;
    } else {
        nr.fft = &nr.fft_256_S;
        ext_arm_copy_f32(hann_window_256, window, ARRAY_SIZE(hann_window_256));
        // 1 - np.exp(-128 / (12500 * 0.05))
        // nr.noise_psd_alpha_up = 0.002556725994413922f;
        // nr.noise_psd_alpha_down = 0.09733158791905794f;
    }
    // Time in seconds

    // nr.noise_psd_alpha_up = 1.0f - expf(-(float)nr.fft->hop / (NR_SAMPLING_RATE * 2.0f));
    // nr.noise_psd_alpha_down = 1.0f - expf(-(float)nr.fft->hop / (NR_SAMPLING_RATE * 0.05f));
}


__STATIC_FORCEINLINE void nr_buf_reset(buf_t *buf) {
    buf->w = 0;
    buf->r = 0;
}

__STATIC_FORCEINLINE void nr_buf_put(buf_t *buf, float val) {
    buf->data[buf->w] = val;
    buf->w = (buf->w + 1) & nr.buf_mask;
}

__STATIC_FORCEINLINE void nr_buf_add(buf_t *buf, float val) {
    buf->data[buf->w] += val;
    buf->w = (buf->w + 1) & nr.buf_mask;
}

__STATIC_FORCEINLINE float nr_buf_get(buf_t *buf) {
    float val = buf->data[buf->r];
    buf->r = (buf->r + 1) & nr.buf_mask;
    return val;
}

__STATIC_FORCEINLINE float nr_buf_get_set(buf_t *buf, float new_val) {
    float val = buf->data[buf->r];
    buf->data[buf->r] = new_val;
    buf->r = (buf->r + 1) & nr.buf_mask;
    return val;
}

__STATIC_FORCEINLINE uint32_t nr_buf_ready_size(buf_t *buf) {
    if (buf->r == buf->w) {
        return buf->cap;
    }
    return (buf->w - buf->r) & nr.buf_mask;
}

__STATIC_FORCEINLINE void nr_buf_lsr(buf_t *buf, uint32_t shift) {
    buf->r = (buf->r - shift) & nr.buf_mask;
}

__STATIC_FORCEINLINE void nr_buf_lsw(buf_t *buf, uint32_t shift) {
    buf->w = (buf->w - shift) & nr.buf_mask;
}

__STATIC_FORCEINLINE void update_mag_mag2(float *z, int i, float agc_k2) {

    // Compute mag^2 and revert AGC
    float real = *z++;
    float imag = *z;
    float mag2_val = (real * real + imag * imag) * agc_k2;
    float mag_val;
    arm_sqrt_f32(mag2_val, &mag_val);

    mag2[i] = mag2_val;
    mag2_avg[i] = mag_val * nr.prev_mag[i];
    nr.prev_mag[i] = mag_val;
}


__STATIC_FORCEINLINE float soft_clip(float x, const float min, const float ptp, const float ptp_inv){
    // Soft clip
    // def smoothstep_clip(x, x_min, ptp):
    //     # Scale and clamp to [0, 1]
    //     x = np.clip((x - x_min) / ptp, 0, 1)
    //     # Polynomial: 3x^2 - 2x^3
    //     return x_min + ptp * (x * x * (3 - 2 * x))
    x = (x - min) * ptp_inv;
    x = CLIP(x, 0.0f, 1.0f);
    x = min + ptp * (x * x * (3 - 2 * x));
    return x;
}

__STATIC_FORCEINLINE void step1() {
    if (nr_buf_ready_size(&nr.in_buf) >= nr.fft->N) {

        /* Compute AGC k */
        float agc_k2 = 0.0f;
        #pragma GCC unroll 4
        for (size_t i = 0; i < nr.fft->N; i++)
        {
            agc_k2 += nr_buf_get(&nr.agc_scales);
        }
        nr_buf_lsr(&nr.agc_scales, nr.fft->overlap);

        agc_k2 = nr.fft->N * nr.fft->N * 10000.0f / agc_k2;

        /* Apply window */
        #pragma GCC unroll 4
        for (size_t i = 0; i < nr.fft->N; i++)
        {
            nr.signal_buf[i] = window[i] * nr_buf_get(&nr.in_buf);
        }
        nr_buf_lsr(&nr.in_buf, nr.fft->overlap);

        /* FFT */
        arm_rfft_fast_f32(&nr.fft->rfft, nr.signal_buf, nr.Z, 0);

        nr.Z[0] = 0.0f;
        nr.Z[1] = 0.0f;

        /* Compute mag squared, update_noise_profile */
        // if (nr.profile_update_delay != 0) {
        if (__builtin_expect(nr.profile_update_delay != 0, 0)) {
            nr.profile_update_delay--;

            // #pragma GCC unroll 4
            #pragma GCC ivdep
            for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++) {
                update_mag_mag2(&nr.Z[i<<1], i, agc_k2);
            }
        } else {
            // #pragma GCC unroll 4
            #pragma GCC ivdep
            for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++) {
                update_mag_mag2(&nr.Z[i<<1], i, agc_k2);
                float diff = mag2[i] - nr.noise_psd[i];
                if (diff > 0) {
                    nr.noise_psd[i] += diff * NR_NOISE_ALPHA_UP;
                } else {
                    nr.noise_psd[i] += diff * NR_NOISE_ALPHA_DOWN;
                }
            }
        }

        nr.next_step = STAGE2;
    }
}

__STATIC_FORCEINLINE void step2() {
    uint32_t *nr_out_write = (uint32_t *)NR_OUT_WRITE;
    float *nr_out_buf = (float *)NR_OUT_BUF;

    /* Compute correction */
    ext_arm_fill_f32(0.0f, raw_gains, nr.filter_high_bin + 1);
    #pragma GCC unroll 4
    // #pragma GCC ivdep
    for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++)
    {
        float gain = (mag2_avg[i] - nr.alpha * nr.noise_psd[i]) / (mag2_avg[i] + 1e-15f);
        // gain = MAX(gain, beta);
        // gain = MIN(gain, 1.0f);
        // gain = CLIP(gain, beta, 1.0f);
        gain = soft_clip(gain, nr.beta, nr.ptp, nr.ptp_inv);
        // gain = sqrtf(gain);
        // arm_sqrt_f32(gain, &gain);
        __ASM("VSQRT.F32 %0,%1" : "=t"(gain) : "t"(gain));
        // freq smoothing
        float g05 = gain * 0.5f;
        float g025 = gain * 0.25f;
        raw_gains[i] += g05;
        raw_gains[i - 1] += g025;
        raw_gains[i + 1] += g025;
    }

    /* Temporal smoothing, applying gain */
    #pragma GCC unroll 4
    // #pragma GCC ivdep
    for (size_t i = nr.filter_low_bin - 1; i < (nr.filter_high_bin + 1); i++)
    {
        nr.gain_smooth[i] += (raw_gains[i] - nr.gain_smooth[i]) * nr.lambda_g;
        nr.Z[i * 2] *= nr.gain_smooth[i];
        nr.Z[i * 2 + 1] *= nr.gain_smooth[i];
    }

    /* IFFT */
    arm_rfft_fast_f32(&nr.fft->rfft, nr.Z, nr.signal_buf, 1);

    /* Window and sum */
    #pragma GCC unroll 4
    // #pragma GCC ivdep
    for (size_t i = 0; i < nr.fft->N; i++)
    {
        nr_buf_add(&nr.out_buf, nr.signal_buf[i] * window[i]);
    }
    nr_buf_lsw(&nr.out_buf, nr.fft->overlap);

    /* Copy output to buf */
    uint32_t write = *nr_out_write;
    for (size_t i = 0; i < nr.fft->hop; i++)
    {
        float val = nr_buf_get_set(&nr.out_buf, 0.0f);
        nr_out_buf[write] = val * NR_NORM_VAL;
        write = (write + 1) & 0x1ff;
    }
    *nr_out_write = write;

    nr.cur_step = STAGE1;
    nr.next_step = STAGE1;
}
