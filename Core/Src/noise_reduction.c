#include "noise_reduction.h"
#include "offsets.h"
#include "external.h"
#include "utils.h"
#include "cw_peak.h"

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

    // input buffer
    buf_t in_buf;

    // Output buffer
    buf_t out_buf;

    nr_fft_t fft_512_S;
    nr_fft_t fft_256_S;
    nr_fft_t *fft;

    // Filters

    uint32_t filter_low_bin;
    uint32_t filter_high_bin;

    // Next interation values
    uint32_t filter_low_bin_next;
    uint32_t filter_high_bin_next;
    uint16_t n_fft_next;

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

    float thr;
} nr_data_t;


static void nr_set_fft_size(uint16_t size);

__STATIC_FORCEINLINE void update_mag_mag2(float *z, int i);
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


static CCMRAM nr_data_t nr;

static CCMRAM float mag2[NR_MAX_MASK_SIZE];
static CCMRAM float mag2_avg[NR_MAX_MASK_SIZE];
static CCMRAM float raw_gains[NR_MAX_MASK_SIZE];

static CCMRAM float window[NR_MAX_NFFT];


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

    nr.in_buf.cap = ARRAY_SIZE(nr.in_buf.data);
    nr.out_buf.cap = ARRAY_SIZE(nr.out_buf.data);

    nr_reset();
    nr.profile_update_delay = 0;
    nr.cur_step = STAGE1;
    nr.next_step = STAGE1;

    nr.filter_low_bin = 2;
    nr.filter_high_bin = 123;
    nr_setup_filters(100, 100, 3000, 3000);
    nr_setup_threshold(0.0f);

    return 0;
}


void nr_reset(void) {
    nr_buf_reset(&nr.in_buf);
    nr_buf_reset(&nr.out_buf);
    ext_arm_fill_f32(0.0f, nr.out_buf.data, ARRAY_SIZE(nr.out_buf.data));

    ext_arm_fill_f32(1.0f, nr.gain_smooth, NR_MAX_MASK_SIZE);
    ext_arm_fill_f32(0.0f, nr.prev_mag, NR_MAX_MASK_SIZE);
    ext_arm_fill_f32(0.1f, nr.noise_psd, NR_MAX_MASK_SIZE);
}

void nr_setup_threshold(float nrthr) {
    if(isnan(nrthr)) {
        nrthr = 0.0f;
    } else {
        nrthr = CLIP(nrthr, 0.0f, 60.0f);
    }
    if (nr.thr != nrthr) {
        nr.thr = nrthr;
        float depth = nr.thr / 30.0f;
        nr.alpha = 1.2f + (depth * 4.0f);
        // float beta = 0.1f - (depth * 0.08f);
        // beta = MAX(beta, 0.01f);
        nr.beta = 0.15f / pow10f_c(depth);
        nr.lambda_g = 0.5f + (depth * 0.2f);
        nr.lambda_g = (1.0f - MIN(nr.lambda_g, 0.7f));

        // Soft clip parameters
        nr.ptp = 1.0f - nr.beta;
        nr.ptp_inv = 1.0f / nr.ptp;
    }
}


void nr_setup_filters(uint32_t low1, uint32_t low2, uint32_t high1, uint32_t high2) {

    // Handle negative int32_t numbers, passed as uint32_t
    if (low1 >> 31) {
        low1 = ~low1 + 1;
    }
    if (low2 >> 31) {
        low2 = ~low2 + 1;
    }
    if (high1 >> 31) {
        high1 = ~high1 + 1;
    }
    if (high2 >> 31) {
        high2 = ~high2 + 1;
    }

    low1 = MIN(low1, low2);
    low1 = CLIP(low1, 50, 1500);
    high1 = MAX(high1, high2);
    high1 = CLIP(high1, low1 + 100, NR_SAMPLING_RATE / 2 - 100);

    if ((high1 - low1) > 3200) {
        nr.n_fft_next = 256;
    } else {
        nr.n_fft_next = 512;
    }

    int32_t low_bin = low1 * nr.n_fft_next / NR_SAMPLING_RATE;
    int32_t high_bin = high1 * nr.n_fft_next / NR_SAMPLING_RATE + 1;

    low_bin = MAX(low_bin, 1);
    high_bin = MIN(high_bin, NR_MAX_MASK_SIZE - 1);
    high_bin = MAX(high_bin, low_bin + 2);

    nr.filter_low_bin_next = low_bin;
    nr.filter_high_bin_next = high_bin;
}

void nr_prepare(void) {
    nr.cur_step = nr.next_step;

    if (nr.cur_step != STAGE2) {

        // Process changed FFT
        if (nr.n_fft_next != nr.fft->N) {
            nr_set_fft_size(nr.n_fft_next);
            if (nr.n_fft_next == 256) {
                for (uint16_t i = 1; i < nr.fft->N - 1; i++)
                {
                    nr.prev_mag[i] = (nr.prev_mag[i * 2] + nr.prev_mag[i * 2 + 1]) * 0.25f;  // 1 / (2 + 2)
                    nr.noise_psd[i] = (nr.noise_psd[i * 2] + nr.noise_psd[i * 2 + 1]) * 0.125f;  // 1 / (4 + 4)
                    nr.gain_smooth[i] = (nr.gain_smooth[i * 2] + nr.gain_smooth[i * 2 + 1]) * 0.5f;
                }
            } else {
                for (uint16_t i = (nr.fft->N / 2) - 1; i > 0; i--)
                {
                    float prev_mag = nr.prev_mag[i] * 2.0f;
                    float noise_psd = nr.noise_psd[i] * 4.0f;
                    float gain_smooth = nr.gain_smooth[i];
                    nr.prev_mag[2 * i] = prev_mag;
                    nr.prev_mag[2 * i + 1] = prev_mag;
                    nr.noise_psd[2 * i] = noise_psd;
                    nr.noise_psd[2 * i + 1] = noise_psd;
                    nr.gain_smooth[2 * i] = gain_smooth;
                    nr.gain_smooth[2 * i + 1] = gain_smooth;
                }
                nr.prev_mag[1] = 0.0f;
                nr.noise_psd[1] = nr.noise_psd[2];
                nr.gain_smooth[1] = 1.0f;
            }
        }

        // Process changed freqs
        if (nr.filter_low_bin > nr.filter_low_bin_next) {
            ext_arm_fill_f32(0.0f, nr.prev_mag, nr.filter_low_bin);
            ext_arm_fill_f32(0.1f, nr.noise_psd, nr.filter_low_bin);
            ext_arm_fill_f32(1.0f, nr.gain_smooth, nr.filter_low_bin);
        }
        if (nr.filter_high_bin < nr.filter_high_bin_next) {
            ext_arm_fill_f32(0.0f, nr.prev_mag + nr.filter_high_bin, ARRAY_SIZE(nr.prev_mag) - nr.filter_high_bin);
            ext_arm_fill_f32(0.1f, nr.noise_psd + nr.filter_high_bin, ARRAY_SIZE(nr.prev_mag) - nr.filter_high_bin);
            ext_arm_fill_f32(1.0f, nr.gain_smooth + nr.filter_high_bin, ARRAY_SIZE(nr.prev_mag) - nr.filter_high_bin);
        }
        nr.filter_low_bin = nr.filter_low_bin_next;
        nr.filter_high_bin = nr.filter_high_bin_next;
    }
}

float nr_apply(float sample)
{
    // Process audio with CW peak filter
    sample = cw_peak_process(sample);

    USE_OEM_TX_FLAG_AS(pTx);

    if (*pTx) {
        return sample;
    }

    USE_OEM_NRE_AS(nre_flag);

    uint32_t *nr_out_write = (uint32_t *)NR_OUT_WRITE;
    uint32_t *nr_out_read = (uint32_t *)NR_OUT_READ;
    float *nr_out_buf = (float *)NR_OUT_BUF;

    if (*nre_flag == 0) {
        *nr_out_write = 0;
        *nr_out_read = 0;
        nr.cur_step = STAGE1;
        nr.next_step = STAGE1;

        nr_buf_reset(&nr.in_buf);
        nr_buf_reset(&nr.out_buf);
        return sample;
    }

    // Copy data to buf
    if (isnan(sample)) {
        sample = 0.0f;
    } else {
        sample = CLIP(sample, -2.0f, 2.0f) * 1e3f;
    }
    nr_buf_put(&nr.in_buf, sample);

    if (nr.cur_step == STAGE1) {
        if (nr_buf_ready_size(&nr.in_buf) >= nr.fft->N) {
            step1();
            nr.next_step = STAGE2;
        }
    } else {
        nr.cur_step = STAGE1;
        nr.next_step = STAGE1;
        step2();
    }

    // Get processed sample
    if (*nr_out_write == *nr_out_read) {
        sample = 0.0f;
    } else {
        sample = nr_out_buf[*nr_out_read];
        *nr_out_read = (*nr_out_read + 1) & 511;
        if (isnan(sample)) {
            sample = 0.0f;
        } else {
            sample *= 1e-3f;
        }
    }
    return sample;
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
    buf->w = (buf->w + 1) & (buf->cap - 1);
}

__STATIC_FORCEINLINE void nr_buf_add(buf_t *buf, float val) {
    buf->data[buf->w] += val;
    buf->w = (buf->w + 1) & (buf->cap - 1);
}

__STATIC_FORCEINLINE float nr_buf_get(buf_t *buf) {
    float val = buf->data[buf->r];
    buf->r = (buf->r + 1) & (buf->cap - 1);
    return val;
}

__STATIC_FORCEINLINE float nr_buf_get_set(buf_t *buf, float new_val) {
    float val = buf->data[buf->r];
    buf->data[buf->r] = new_val;
    buf->r = (buf->r + 1) & (buf->cap - 1);
    return val;
}

__STATIC_FORCEINLINE uint32_t nr_buf_ready_size(buf_t *buf) {
    if (buf->r == buf->w) {
        return buf->cap;
    }
    return (buf->w - buf->r) & (buf->cap - 1);
}

__STATIC_FORCEINLINE void nr_buf_lsr(buf_t *buf, uint32_t shift) {
    buf->r = (buf->r - shift) & (buf->cap - 1);
}

__STATIC_FORCEINLINE void nr_buf_lsw(buf_t *buf, uint32_t shift) {
    buf->w = (buf->w - shift) & (buf->cap - 1);
}

__STATIC_FORCEINLINE void update_mag_mag2(float *z, int i) {

    // Compute mag^2 and revert AGC
    float real = *z++;
    float imag = *z;
    float mag2_val = real * real + imag * imag;
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
    /* Apply window */
    #pragma GCC unroll 4
    for (size_t i = 0; i < nr.fft->N; i++)
    {
        float v = nr_buf_get(&nr.in_buf);
        nr.signal_buf[i] = window[i] * v;
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
            update_mag_mag2(&nr.Z[i<<1], i);
        }
    } else {
        // #pragma GCC unroll 4
        #pragma GCC ivdep
        for (size_t i = nr.filter_low_bin; i < nr.filter_high_bin; i++) {
            update_mag_mag2(&nr.Z[i<<1], i);
            float diff = mag2[i] - nr.noise_psd[i];
            if (diff > 0) {
                nr.noise_psd[i] += diff * NR_NOISE_ALPHA_UP;
            } else {
                nr.noise_psd[i] += diff * NR_NOISE_ALPHA_DOWN;
            }
        }
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
        float gain = 1.0f - (nr.alpha * nr.noise_psd[i]) / (mag2_avg[i] + 1e-15f);
        // gain = MAX(gain, beta);
        // gain = MIN(gain, 1.0f);
        // gain = CLIP(gain, beta, 1.0f);
        gain = soft_clip(gain, nr.beta, nr.ptp, nr.ptp_inv);
        // gain = sqrtf(gain);
        arm_sqrt_f32(gain, &gain);
        // __ASM("VSQRT.F32 %0,%1" : "=t"(gain) : "t"(gain));
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
}
