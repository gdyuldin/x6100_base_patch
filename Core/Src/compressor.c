#include "compressor.h"

#include <dsp/controller_functions.h>

#include "offsets.h"

#include "noise_reduction.h"

#include "math.h"
#include "stdbool.h"
#include "log10f.c"
#include "powf.c"
#include "sin_cos.c"
#include "stdarg.h"
#include "stdio.h"
#include "iirdecim.h"
// #include <dsp/fast_math_functions.h>
// #include <dsp/support_functions.h>


// #define MAX(a, b) (a > b ? a : b)
// #define MIN(a, b) (a < b ? a : b)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

#define I2C_REG_T(fields) typedef union {uint32_t i; struct fields v;}

/**
 * General constants
 */

#define SWR_SCAN 0x00010


// Flow complex samples size
#define FLOW_SEQ_SAMPLES 512
#define CFLOAT32_BYTES (sizeof(float) * 2)
#define CFLOAT16_BYTES (sizeof(uint16_t) * 2)

/**
 * Compressor/gate constants
 */

 // Sine signal level (zero to peak) on SSB, which produces expected power
#define UNITY_LVL -41.0f

// Level of microphone noise
#define NOISE_LVL -82.0f

// Delay in samples
#define COMP_DELAY 60
#define ATT_ALPHA 0.035957992f
#define RELEASE_ALPHA 0.00061015395f
#define RATIO_GATE 0.25f
#define TH_COMP (NOISE_LVL + 25.0f)


/**
 * DC blockers constants
 */

#define TX_DC_BLOCKER_ALPHA (0.05f)
#define RX_AM_DC_BLOCKER_ALPHA (0.03f)
#define RX_FM_DC_BLOCKER_ALPHA (0.01f)


/**
 * I2C registers
 */
typedef enum
{
    x6100_sple_atue_trx = 12,
    x6100_vi_vm,
    x6100_rfg_txpwr = 15,  // plus fft span
    x6100_flow_fm_emp = 18,
    x6100_dac_adc_offsets = 19,
    x6100_dnfcnt_dnfwidth_dnfe = 24,
    x6100_cmplevel_cmpe = 25,
    x6100_if_shift = 35,
    x6100_tx_filter = 38,
} x6100_cmd_enum_t;

typedef enum __attribute__((__packed__))
{
    x6100_flow_fp32 = 0,
    x6100_flow_bf16,
} x6100_flow_fmt_t;


I2C_REG_T({
    uint8_t flow_fp16: 1;
    uint8_t fm_emp: 1;
}) x6100_reg_flow_fmt_fm_emp_t;

I2C_REG_T({
    uint16_t adc_dac_gain_offset;  // bf16
    uint16_t dac_gain_offset;  // bf16
}) x6100_reg_dac_adc_offsets_t;

I2C_REG_T({
    uint16_t low;
    uint16_t high;
}) x6100_reg_tx_filter_t;


enum __attribute__((__packed__)) rx_tx_process_state {
    RX_TX_STATE_NORMAL,
    RX_TX_STATE_PIN_DISCHARGE,
    RX_TX_STATE_MUTE,
    RX_TX_STATE_WAIT_PIN_SWITCH,
};


/**
 * Ring buffer struct
 */
struct ring_buf {
    float *data;
    uint32_t w;
    uint32_t r;
    uint32_t size;
};

/**
 * DC blocker struct
 */
struct dc_blocker_t {
    float xm1;
    float ym1;
};

typedef struct __packed {
    uint32_t lo_freq;
    uint8_t flow_fmt;
    uint8_t flow_seq_n: 4;
    uint8_t flow_seq_total: 4;
    uint8_t vary_freq: 1;
    uint8_t fft_dec: 3;
    uint32_t _pad1: 12;
    uint32_t _pad2;
} flow_info_t;


/**
 * State struct
 */

typedef struct {
    // multiplier for output (before DAC)
    float dac_output_coeff;
    // multiplier for input (after ADC)
    float adc_input_coeff;

    struct {
        /* Compressor data */
        float threshold;
        float ratio_comp;
        float makeup;

        float l1_comp;
        float l1_gate;
        float corr;
        struct ring_buf dline;
        float dline_data[COMP_DELAY];
        struct ring_buf squared_acc;
        float squared_acc_data[COMP_DELAY];
        float squared_sum;
    } comp;

    struct dc_blocker_t tx_dc_blocker;

    struct {
        /* TX level control data */
        // tx amp coeffs
        float ssb;
        float cw;
        float am;
        float fm;

        float adc_dac_gain_offset;  // For in audio / out rf correction
        float dac_gain_offset;  // For per-band out correction
        bool outdated;
    } tx_amp_coeffs;

    /* AM/FM RX processing data */
    // rx am/fm DC blocker
    struct dc_blocker_t rx_am_dc_blocker;
    struct dc_blocker_t rx_fm_dc_blocker;

    struct {
        // rx fm_sql
        uint32_t counter;
        float iq_squared_sum;
        uint32_t iq_squared_cnt;
        float iq_rms_db;

    } fm_sql;

    /* ANF data */
    struct {
        bool enabled;
        float an;
        float mean_squared;
    } anf;

    /* RX IQ offset */
    struct {
        // Average I value (I offset)
        float i_mean;
        // Average Q value (Q offset)
        float q_mean;
        // Block processing id (after tx->rx)
        uint32_t step;
        int32_t min_i;
        int32_t min_q;
        // Saved I offset for PIN discharging
        float i_mean_fix;
        // Saved Q offset for PIN discharging
        float q_mean_fix;
        // Last TX flag
        uint8_t last_tx;
        enum rx_tx_process_state state;
    } rx_iq_corr;

    /* General data */
    struct {
        enum mod_t modulation;
        uint32_t att;
        uint32_t pre;
    } prev_state;
    uint8_t tx_flag;
    bool swr_scan;
    uint8_t band_id;

    struct {
        uint32_t rfg_txpwr;
        uint32_t cmplevel_cmpe;
        uint32_t dnfcnt_dnfwidth_dnfe;
        uint32_t if_shift;
        x6100_reg_flow_fmt_fm_emp_t flow_fm_emp;
        x6100_reg_dac_adc_offsets_t dac_adc_offsets;
        x6100_reg_tx_filter_t tx_filters;
    } i2c_raw;

    struct {
        x6100_flow_fmt_t fmt;
        uint8_t fft_dec;
        uint8_t collecting_delay; // Delay of collecting after changing freq.
        uint8_t data[CFLOAT32_BYTES * FLOW_SEQ_SAMPLES * 2];  // Buffer with max allowed size.
        uint8_t *write_p;
        uint8_t *read_p;
        uint8_t sample_size;  // Size of dingle sample in bytes
        uint16_t desired_samples;  // Count of desired samples to collect
        struct {
            uint32_t sum;
            uint8_t cnt;
        } avg_freq;
    } flow;

    // FM demodulation
    struct {
        cfloat_t iq_history[3];
        float avg_k;
        float hpf_env;
        arm_biquad_casd_df1_inst_f32 iir_snr_detector;
        float iir_snr_detector_coeffs[5];
        float iir_snr_detector_state[4];
        bool emphasis_on;
        // De-emphasis filter
        arm_biquad_casd_df1_inst_f32 deemp_filter;
        float deemp_filter_coeffs[5];
        float deemp_filter_state[4];
        // Pre-emphasis filter
        arm_biquad_casd_df1_inst_f32 preemp_filter;
        float preemp_filter_coeffs[5];
        float preemp_filter_state[4];
        float phase;
    } fm_demod;

    flow_info_t flow_info;
    /* Debug and pad */
    // tone
    struct {
        bool on;
        int32_t freq;
        float step;
        float angle;
    } if_shift;
    float freq_shift_angle;

} data_t;
// } __attribute__ ((aligned (16))) data_t;

// To check. Should be 12
// static uint32_t size = sizeof(data->flow_reserved_3);

// #define DATA_P (STACK_ADDR - sizeof(data_t))

// static data_t *data = (data_t*)DATA_P;

data_t data_arr __attribute((section(".ccmram")));

static data_t *data = &data_arr;

// #define CCM_DATA_P (0x10000000 + 0x10000 - sizeof(data_t))
// __attribute((section(".ccmram"))) static data_t *data  = (data_t*)CCM_DATA_P;
// __attribute((section(".ccmram"))) static data_t *data;


/**
 * Actual values pointers
 */

// Compressor values
static uint8_t *cmp_enabled = (uint8_t *)CMP_ENABLED_VALUE;
static uint8_t *cmp_level = (uint8_t *)CMP_LEVEL_VALUE;

// Squelch value
static uint8_t *sql = (uint8_t *)SQL_VALUE;

// UART flow fields
static uint32_t *flow_reserved_3 = (uint32_t*)FLOW_RESERVED_ADDR;


// I2C registers values start pointer
static uint32_t *i2c_regs = (uint32_t *)I2C_REGS_ADDR;

static float *am_carrier_lvl = (float *)AM_CARRIER_LEVEL_VALUE;
static float *fm_depth_of_mod = (float *)FM_DEPTH_OF_MOD_VALUE;

static volatile uint32_t *freq_plus_rit = (uint32_t *)FREQ_PLUS_RIT;
static uint8_t *stop_copy_flow = (uint8_t *)STOP_COPY_FLOW;
static uint32_t *flow_n_samples = (uint32_t *)FLOW_N_SAMPLES; // Count of samples to send in a block, 512
static uint32_t *flow_samples_counter = (uint32_t *)FLOW_SAMPLES_COUNTER;  // Count of already collected samples in OEM function
static float *flow_samples_cplx = (float *)FLOW_SAMPLES_CPLX;  // Pointer to array of flow samples


static void flow_collecting_reset(void);

/**
 * Db <-> linear conversion
 */

inline __attribute__((always_inline)) float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline __attribute__((always_inline)) float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-16f);
}



/**
 * Operations with a ring buffer
 */

void ring_buf_put(struct ring_buf *buf, float val) {
    buf->data[buf->w] = val;
    if (buf->w == buf->size - 1) {
        buf->w = 0;
    } else {
        buf->w++;
    }
}

float ring_buf_get(struct ring_buf *buf) {
    float val = buf->data[buf->r];
    if (buf->r == buf->size - 1) {
        buf->r = 0;
    } else {
        buf->r++;
    }
    return val;
}

uint8_t ring_buf_full(struct ring_buf *buf) {
    return buf->w == buf->r;
}

void ring_buf_reset(struct ring_buf *buf) {
    buf->w = 0;
    buf->r = 0;
}


inline float dc_blocker(float val, float k, struct dc_blocker_t *dc) {
    float tmp = val - dc->xm1 + k * dc->ym1;
    dc->xm1 = val;
    dc->ym1 = tmp;
    return tmp;
}

float soft_limiter(float val, float max_val) {
    const float th = max_val * 0.5f;
    float x;
    if (val > th) {
        x = th  / val;
        val = (1.0f - x) * (max_val - th) + th;
    } else if (val < -th) {
        x = -th  / val;
        val = -((1.0f - x) * (max_val - th) + th);
    }
    return val;
}


static inline void update_tx_filter_params(uint16_t low, uint16_t high) {
    float rate = *(float *)TX_SAMPLING_RATE_12_5;
    void *flt_S = (void *)BIQUAD_TX_FLT_INST;
    setup_biquad_filter(rate, low, high, flt_S, 1);
}

inline static void fast_iq_offset_counter_setup() {
    int32_t *input_data = *(int32_t **)INPUT_RF_SIGNAL_INT_ADDR;
    uint32_t samples_count = *(uint32_t*)SAMPLES_COUNT_VALUE;

    // Increase counter
    if (data->rx_iq_corr.step <= 800) {
        data->rx_iq_corr.step++;
    }
    if (*tx_flag != data->rx_iq_corr.last_tx) {
        data->rx_iq_corr.last_tx = *tx_flag;
        if (!*tx_flag) {
            data->rx_iq_corr.step = 0;
            data->rx_iq_corr.state = RX_TX_STATE_MUTE;
        }
    }

    if (data->rx_iq_corr.state == RX_TX_STATE_MUTE) {
        if (data->rx_iq_corr.step >= 95) {
            // Mute -> wait switch
            data->rx_iq_corr.state = RX_TX_STATE_WAIT_PIN_SWITCH;
            data->rx_iq_corr.min_i = (1 << 31);
            data->rx_iq_corr.min_q = (1 << 31);
        }
    } else if (data->rx_iq_corr.state == RX_TX_STATE_WAIT_PIN_SWITCH) {
        if (data->rx_iq_corr.step > 160) {
            // Too long seach state, wait -> discharge
            data->rx_iq_corr.state = RX_TX_STATE_PIN_DISCHARGE;
            data->rx_iq_corr.i_mean_fix = data->rx_iq_corr.i_mean;
            data->rx_iq_corr.i_mean = data->rx_iq_corr.min_i;
            data->rx_iq_corr.q_mean_fix = data->rx_iq_corr.q_mean;
            data->rx_iq_corr.q_mean = data->rx_iq_corr.min_q;
        } else {
            // searching for min value
            int32_t min_i = (1<<30);
            int32_t min_q = (1<<30);
            for (size_t i = 0; i < samples_count * 2; i+=2) {
                int32_t val = ((uint32_t)input_data[i] >> 0x10) | (input_data[i] << 0x10);
                min_i = MIN(min_i, val);

                val = ((uint32_t)input_data[i + 1] >> 0x10) | (input_data[i + 1] << 0x10);
                min_q = MIN(min_q, val);
            }
            // Check for increasing values (pin switch) to switch the next state
            if (min_i < data->rx_iq_corr.min_i) {
                data->rx_iq_corr.min_i = min_i;
            } else if (min_q < data->rx_iq_corr.min_q) {
                data->rx_iq_corr.min_q = min_q;
            } else {
                // Val increases, wait -> discharge
                data->rx_iq_corr.state = RX_TX_STATE_PIN_DISCHARGE;
                data->rx_iq_corr.i_mean_fix = data->rx_iq_corr.i_mean;
                data->rx_iq_corr.i_mean = min_i;
                data->rx_iq_corr.q_mean_fix = data->rx_iq_corr.q_mean;
                data->rx_iq_corr.q_mean = min_q;
            }
        }
    } else if (data->rx_iq_corr.state == RX_TX_STATE_PIN_DISCHARGE) {
        if (data->rx_iq_corr.step >= 800) {
            // discharge -> normal process
            data->rx_iq_corr.state = RX_TX_STATE_NORMAL;
        }
    }
}


static inline void flow_collecting_reset(void) {
    // Reset collecting to prevent sending
    *stop_copy_flow = false;
    *flow_samples_counter = 0;
    // reset flow_data pointers
    data->flow.write_p = data->flow.data;
    data->flow.read_p = data->flow.data;
    data->flow.avg_freq.cnt = 0;
    data->flow.avg_freq.sum = 0;
    iirdecim_reset();
}

static inline void *copy_flow_f32(float *pSrc, float *pDst, size_t n_src_cmplx_samples) {
    iir_decim_iq_n(data->flow.fft_dec, pSrc, pDst, n_src_cmplx_samples);
    n_src_cmplx_samples >>= data->flow.fft_dec;

    return pDst + (n_src_cmplx_samples << 1);
}

static inline void *copy_flow_b16(float *pSrc, void *pDst, size_t n_src_cmplx_samples) {
    float *tmp;
    size_t n_blocks;
    iir_decim_iq_n(data->flow.fft_dec, pSrc, pSrc, n_src_cmplx_samples);
    n_src_cmplx_samples >>= data->flow.fft_dec;
    uint16_t *pfDst = (uint16_t*)pDst;
    tmp = pSrc;
    n_blocks = n_src_cmplx_samples >> 1u;
    while (n_blocks) {
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        n_blocks--;
    }
    // Copy rest
    n_blocks = n_src_cmplx_samples & 1u;
    while (n_blocks) {
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        *pfDst++ = *((uint16_t*)&(*tmp++) + 1);
        n_blocks--;
    }
    return pfDst;
}

static inline void set_flow_params(x6100_flow_fmt_t fmt) {
    uint16_t size;
    data->flow.fmt = fmt;
    switch (fmt) {
        case x6100_flow_fp32:
            data->flow.sample_size = CFLOAT32_BYTES;
            size = FLOW_SEQ_SAMPLES;
            break;
            case x6100_flow_bf16:
            data->flow.sample_size = CFLOAT16_BYTES;
            size = FLOW_SEQ_SAMPLES * 2;
            break;

        default:
            return;
    }
    size_t block_size = FLOW_SEQ_SAMPLES * CFLOAT32_BYTES / data->flow.sample_size;
    data->flow_info.flow_seq_total = size / block_size;
    data->flow.desired_samples = size;
    flow_collecting_reset();

}

// Patched version of copying flow data

uint32_t copy_flow(float *p_Dst) {
    uint32_t copied = 0;
    uint32_t *custom_flow_reserved_3 = (uint32_t *)&data->flow_info;
    uint32_t avg_freq;

    data->flow_info.flow_seq_n = 0xf;
    // data->flow_info.pad = TIM2->ARR;
    // TIM2->ARR = 4000 / 5;

    // TIM2->PSC = 80;
    // TIM2->ARR = 16000;
    // TIM2->EGR = TIM_EGR_UG;
    // // Force auto reloading
    // TIM2->CR1 |= TIM_CR1_ARPE;
    // if ((data->flow.read_p == data->flow.data) && (TIM2->PSC != 20)) {
    //     TIM2->PSC = 20;
    // } else {
    //     TIM2->PSC = 5;
    // }
    // TIM2->SR
    if (data->flow.data + (data->flow.desired_samples * data->flow.sample_size) <= data->flow.write_p) {
        arm_copy_f32((float *)data->flow.read_p, p_Dst, (*flow_n_samples) << 1);
        uint32_t copied_bytes = (*flow_n_samples * 8);
        data->flow_info.flow_seq_n = (data->flow.read_p - data->flow.data) / copied_bytes;
        avg_freq = data->flow.avg_freq.sum / data->flow.avg_freq.cnt;
        if (avg_freq != data->flow_info.lo_freq) {
            data->flow_info.lo_freq = avg_freq;
            data->flow_info.vary_freq = true;
        } else {
            data->flow_info.vary_freq = false;
        }
        flow_reserved_3[0] = custom_flow_reserved_3[0];
        flow_reserved_3[1] = custom_flow_reserved_3[1];
        flow_reserved_3[2] = custom_flow_reserved_3[2];
        data->flow.read_p += copied_bytes;
        copied = 1;
        if (data->flow.read_p >= data->flow.write_p) {
            // All data sent, reset counters
            flow_collecting_reset();
        }
  }
  return copied;
}


static void flow_collecting_at_end(void) {
    size_t remain_samples = (data->flow.data + (data->flow.desired_samples * data->flow.sample_size) - data->flow.write_p) / data->flow.sample_size;
    // Collect data.
    if (remain_samples > 0) {
        // Add prev freq to avg acc
        data->flow.avg_freq.sum += *freq_plus_rit;
        data->flow.avg_freq.cnt += 1;
        if (data->flow.write_p == data->flow.data) {
            // Remember freq and fmt at filling 1st block
            data->flow_info.flow_fmt = data->flow.fmt;
            data->flow_info.lo_freq = *freq_plus_rit;
            iirdecim_reset();
        }

        ssize_t offset = -1;
        if (remain_samples > (*flow_n_samples >> data->flow.fft_dec)) {
            if (*flow_samples_counter) {
                offset = 0;
                // Reset samples counter
                *flow_samples_counter = 0;
            }
        } else {
            if (*flow_samples_counter != 0) {
                offset = *flow_samples_counter - 128;
            } else {
                // copy last block
                offset = 384;
            }
        }
        if (offset >= 0) {
            // data->flow.write_p = copy_flow_f32(flow_samples_cplx + (offset << 1), (float *)data->flow.write_p, 128);
            if (data->flow.fmt == x6100_flow_fp32) {
                data->flow.write_p = copy_flow_f32(flow_samples_cplx + (offset << 1), (float *)data->flow.write_p, 128);
            } else {
                data->flow.write_p = copy_flow_b16(flow_samples_cplx + (offset << 1), (void *)data->flow.write_p, 128);
            }
        }
    }
}

static void fm_demod_init(void) {
    arm_fill_f32(0.0f, (float *)data->fm_demod.iq_history, sizeof(data->fm_demod.iq_history) / 4);
    data->fm_demod.avg_k = 0.0f;
    data->fm_demod.hpf_env = 0.0f;

    data->fm_demod.iir_snr_detector.numStages = 1;
    data->fm_demod.iir_snr_detector.pCoeffs = data->fm_demod.iir_snr_detector_coeffs;
    data->fm_demod.iir_snr_detector.pState = data->fm_demod.iir_snr_detector_state;
    arm_fill_f32(0.0f, data->fm_demod.iir_snr_detector_state, sizeof(data->fm_demod.iir_snr_detector_state) / 4);
    data->fm_demod.iir_snr_detector_coeffs[0] = 0.11735104f;
    data->fm_demod.iir_snr_detector_coeffs[1] = -0.23470207f;
    data->fm_demod.iir_snr_detector_coeffs[2] = 0.11735104f;
    data->fm_demod.iir_snr_detector_coeffs[3] = -0.82523238f;
    data->fm_demod.iir_snr_detector_coeffs[4] = -0.29463653f;

    // De-emphasis filter
    data->fm_demod.deemp_filter.numStages = 1;
    data->fm_demod.deemp_filter.pCoeffs = data->fm_demod.deemp_filter_coeffs;
    data->fm_demod.deemp_filter.pState = data->fm_demod.deemp_filter_state;
    arm_fill_f32(0.0f, data->fm_demod.deemp_filter_state, sizeof(data->fm_demod.deemp_filter_state) / 4);
    data->fm_demod.deemp_filter_coeffs[0] = 1.0f;
    data->fm_demod.deemp_filter_coeffs[1] = 0.4133537804899683f;
    data->fm_demod.deemp_filter_coeffs[2] = 0.0f;
    data->fm_demod.deemp_filter_coeffs[3] = 0.5866462195100317f;
    data->fm_demod.deemp_filter_coeffs[4] = 0.0f;

    // Pre-emphasis filter
    data->fm_demod.preemp_filter.numStages = 1;
    data->fm_demod.preemp_filter.pCoeffs = data->fm_demod.preemp_filter_coeffs;
    data->fm_demod.preemp_filter.pState = data->fm_demod.preemp_filter_state;
    arm_fill_f32(0.0f, data->fm_demod.preemp_filter_state, sizeof(data->fm_demod.preemp_filter_state) / 4);
    data->fm_demod.preemp_filter_coeffs[0] = 1.0f;
    data->fm_demod.preemp_filter_coeffs[1] = -0.3441537868654123f;
    data->fm_demod.preemp_filter_coeffs[2] = 0.0f;
    data->fm_demod.preemp_filter_coeffs[3] = -0.6558462131345877f;
    data->fm_demod.preemp_filter_coeffs[4] = 0.0f;

    data->fm_demod.emphasis_on = 1;
}

/**
 * Setup data storage. Inject, calls at startup
 */
__attribute__((optimize("O1"))) void init_data(void) {
    // uint32_t *area_end = (uint32_t*)0x20030000;
    // uint32_t *area_start = area_end - sizeof(data_t) / sizeof(uint32_t);
    uint32_t *area_start = (uint32_t*)data;
    uint32_t *area_end = (uint32_t*)data + sizeof(data_t) / sizeof(uint32_t);

    for (;area_start < area_end; area_start++) {
        *area_start = 0;
    }
    data->comp.dline.data = data->comp.dline_data;
    data->comp.dline.size = sizeof(data->comp.dline_data) / sizeof(*data->comp.dline_data);
    data->comp.squared_acc.data = data->comp.squared_acc_data;
    data->comp.squared_acc.size = sizeof(data->comp.squared_acc_data) / sizeof(*data->comp.squared_acc_data);

    set_flow_params(x6100_flow_fp32);
    data->if_shift.angle = 0.0f;
    data->if_shift.step = 0.0f;
    data->if_shift.on = false;
    data->if_shift.freq = 0;

    // init fft_dec instances
    iirdecim_init();

    fm_demod_init();

    data->i2c_raw.tx_filters.v.low = 160;
    data->i2c_raw.tx_filters.v.high = 3000;

    // Noise reduction init
    nr_init();
}


/**
 * Update signal processing parameters. Inject, calls at beginning of the DMA process.
 */

static void reset_filters_states_on_changes() {
    bool *reset = (bool*)RESET_FILTERS_STATE;
    if (*reset) {
        return;
    }
    // arm_fill_f32(0.0,data.f.i_lpf.pState,0x14);                      0x20009178
    // arm_fill_f32(0.0,data.f.q_lpf.pState,0x14);                      0x20009244

    // arm_fill_f32(0.0,data.f.a_filters_1[1].pState,0x14);             0x200093dc
    // arm_fill_f32(0.0,data.f.a_filters_1[0].pState,0x14);             0x20009310
    // arm_fill_f32(0.0,data.f.a_filters_2[1].pState,0x14);             0x20009574
    // arm_fill_f32(0.0,data.f.a_filters_2[0].pState,0x14);             0x200094a8

    // arm_fill_f32(0.0,data.f.fir_decim_8[0].pState,0x47);             0x20008af0
    // arm_fill_f32(0.0,data.f.fir_decim_8[0].buf,8);                   0x20008c10
    // data.f.fir_decim_8[0].i = 0;                                     0x20008c0c
    // arm_fill_f32(0.0,data.f.fir_decim_8[1].pState,0x47);             0x20008d3c
    // arm_fill_f32(0.0,data.f.fir_decim_8[1].buf,8);                   0x20008e5c
    // data.f.fir_decim_8[1].i = 0;                                     0x20008e58

    // arm_fill_f32(0.0,data.f.fir_decim_4[0].pState,0x27);             0x200087e8
    // arm_fill_f32(0.0,data.f.fir_decim_4[0].arr_4,4);                 0x20008888
    // data.f.fir_decim_4[0].zero = 0;                                  0x20008884
    // arm_fill_f32(0.0,data.f.fir_decim_4[1].pState,0x27);             0x20008934
    // arm_fill_f32(0.0,data.f.fir_decim_4[1].arr_4,4);                 0x200089d4
    // data.f.fir_decim_4[1].zero = 0;                                  0x200089d0

    // data.f.fir_decim_2.zero = 0;                                     0x20008fac
    // arm_fill_f32(0.0,data.f.fir_decim_2.f_arr_2,2);                  0x20008fb0
    // arm_fill_f32(0.0,data.f.fir_decim_2.pState,0x25);                0x20008f18

    // arm_fill_f32(0.0,data.f.fir_interp_8.pState,8);                  0x200090c4
    // arm_fill_f32(0.0,data.f.fir_interp_8.f_arr_8,8);                 0x200090e4


    uint32_t *att = (uint32_t*)ATT;
    uint32_t *pre = (uint32_t*)PRE;

    struct fbuf {
        float*  a;
        uint32_t s;
    };


    struct fbuf interp_bufs[] = {
        // interp
        {(float*)FIR_INTERP_8_STATE, 8},
        {(float*)FIR_INTERP_8_BUF, 8},

        // filters 1-2
        {(float*)RX_FILTER_1_0_STATE, 0x14},
        {(float*)RX_FILTER_1_1_STATE, 0x14},
        {(float*)RX_FILTER_2_0_STATE, 0x14},
        {(float*)RX_FILTER_2_1_STATE, 0x14},

        // decim_2
        {(float*)FIR_DECIM_2_BUF, 2},
        {(float*)FIR_DECIM_2_STATE, 0x25},
    };

    bool clear = false;
    uint32_t *decim2_i = (uint32_t*)FIR_DECIM_2_I;

    if (*modulation != data->prev_state.modulation) {
        data->prev_state.modulation = *modulation;
        clear = true;
    }
    if (*pre != data->prev_state.pre) {
        data->prev_state.pre = *pre;
        clear = true;
    }
    if (*att != data->prev_state.att) {
        data->prev_state.att = *att;
        clear = true;
    }

    if (clear) {
        for (size_t i = 0; i < ARRAY_SIZE(interp_bufs); i++) {
            arm_fill_f32(0.0f, interp_bufs[i].a, interp_bufs[i].s);
        }
        *decim2_i = 0;
    }
}

void configure(void) {
    reset_filters_states_on_changes();

    // Reset coefficients on SWR scan
    bool swr_scan = i2c_regs[x6100_sple_atue_trx] & SWR_SCAN;
    if (swr_scan != data->swr_scan) {
        data->swr_scan = swr_scan;
        data->tx_amp_coeffs.outdated = true;
    }

    uint8_t band_id = (uint8_t)(i2c_regs[x6100_vi_vm] >> 8);
    if (band_id != data->band_id) {
        data->band_id = band_id;
        data->tx_amp_coeffs.outdated = true;
    }

    if (data->tx_amp_coeffs.outdated) {
        float pwr = (uint8_t)(data->i2c_raw.rfg_txpwr >> 8) * 0.1f;
        tx_coeff_calc(pwr);
    }
    fast_iq_offset_counter_setup();

    // Reset ring buffers (for compressor) on RX/TX state change
    if (data->tx_flag != *tx_flag) {
        data->tx_flag = *tx_flag;
        if (data->tx_flag) {
            ring_buf_reset(&data->comp.dline);
        }
    }
    nr_setup_filters();
}


/**
 * Patches for collecting flow. Called at end of the DMA process.
 */

void dma_end(void) {
    flow_collecting_at_end();
}

/**
 * Convert int IQ to float and apply offsets
 */
// void apply_rx_iq_offset_impl(float *i, float *q);


void apply_rx_iq_offset(void) {
    // IQ incoming data manipulation

    float *i, *q;
    uint32_t *sp_val;

    __asm__ __volatile__(
        "MOV %0, sp\n"
        : "=r"(sp_val)
        :
        :
    );

    i = (float *)sp_val;
    q = (float *)sp_val + 1;

    float a = 0.001f;

    if (data->rx_iq_corr.state == RX_TX_STATE_PIN_DISCHARGE) {
        // Discharge step
        const float k = 1.0f - 0.000051f;
        data->rx_iq_corr.i_mean = (data->rx_iq_corr.i_mean - data->rx_iq_corr.i_mean_fix)*k + data->rx_iq_corr.i_mean_fix;
        data->rx_iq_corr.q_mean = (data->rx_iq_corr.q_mean - data->rx_iq_corr.q_mean_fix)*k + data->rx_iq_corr.q_mean_fix;
        a = 0.003f;
    }

    if ((data->rx_iq_corr.state == RX_TX_STATE_MUTE) || (data->rx_iq_corr.state == RX_TX_STATE_WAIT_PIN_SWITCH)) {
        *i = 0.0f;
        *q = 0.0f;
    } else {
        float i_corr = *i - data->rx_iq_corr.i_mean;
        float q_corr = *q - data->rx_iq_corr.q_mean;

        data->rx_iq_corr.i_mean += i_corr * a;
        data->rx_iq_corr.q_mean += q_corr * a;

        *i = i_corr;
        *q = q_corr;
    }
}

/**
 * IF shift
 */

void if_shift(void) {
    if (!data->if_shift.on || (data->if_shift.step == 0.0f)) {
        return;
    }
    // Size 128 x 2
    float *iq = (float*) IQ_RF_FLOAT_IN;
    float *stop = iq + 256;
    float angle = data->if_shift.angle;
    do
    {
        float i, q, i_a, q_a;
        i = iq[0];
        q = iq[1];
        sin_cos(angle, &q_a, &i_a);
        float m1 = i * i_a;
        float m2 = q * q_a;
        float m3 = i * q_a;
        float m4 = q * i_a;
        i = m1 - m2;
        q = m3 + m4;
        iq[0] = i;
        iq[1] = q;

        iq += 2;

        angle += data->if_shift.step;
        if (angle > M_TWOPI_F) {
            angle -= M_TWOPI_F;
        } else if (angle < 0) {
            angle += M_TWOPI_F;
        }
    } while (iq != stop);
    data->if_shift.angle = angle;
}

int32_t tx_if_shift(int32_t lo_freq_shift) {
    if (data->if_shift.on) {
        lo_freq_shift += data->if_shift.freq;
    }
    return lo_freq_shift;
}

/**
 * Set IQ scale on changing TX power
 */
__noinline void tx_coeff_calc(float pwr) {
    data->tx_amp_coeffs.outdated = false;
    float *am_depth_of_mod = (float *)AM_DEPTH_OF_MOD_VALUE;
    float k;

    // flow_reserved_3[0] = data->band_id;
    // flow_reserved_3[1] = *(uint32_t*)&adc_dac_gain_offset;
    // flow_reserved_3[2] = *(uint32_t*)&dac_band_gain_offset;

    data->dac_output_coeff = db2lin(data->tx_amp_coeffs.adc_dac_gain_offset + data->tx_amp_coeffs.dac_gain_offset);
    data->adc_input_coeff = 1.0f / db2lin(data->tx_amp_coeffs.adc_dac_gain_offset);
    if (pwr >= 0) {
        float pow_scale = pwr / 10.0f;
        if (pow_scale <= 0.0f) {
            k = 1.0f;
        } else {
            k = sqrt_f32(pow_scale);
        }
    } else {
        k = 1.0f;
    }
    // set coeffs
    k *= data->dac_output_coeff;

    data->tx_amp_coeffs.fm = 0.13485983319932224f * k;

    // AM carrier for 100% - 0.09536030256492743
    if (data->swr_scan) {
        *am_carrier_lvl = 0.75f;
    } else {
        *am_carrier_lvl = 0.047680151282463716f * k;
    }
    *am_depth_of_mod = 3.85f * k;

    data->tx_amp_coeffs.ssb = k;

    data->tx_amp_coeffs.cw = 0.09536030256492745f * k;

    // Set FM depth of mod w.r.t audio level to achieve 2.5 kHz depth of modulation
    *fm_depth_of_mod = 32.5f * data->adc_input_coeff;
}

/**
 * Compressor, limiter
 */
__attribute__((noinline, optimize("O2"))) void compress(float *pval) {
    switch (*modulation) {
        // case MOD_LSB:
        case MOD_LSB_D: // lsb-d
        case MOD_USB_D: // usb-d
            *pval *= data->adc_input_coeff;
            return;
            break;
        case MOD_CW: // cw
        case MOD_CWR: // cwr
            return;
            break;
        default:
            break;
    }
    float val = *pval;

    // Remove DC offset
    val = dc_blocker(val, (1.0f - TX_DC_BLOCKER_ALPHA), &data->tx_dc_blocker);
    val *= data->adc_input_coeff;

    // Invert (make enabled by default)
    if (*cmp_enabled) {
        *pval = val;
        return;
    }

    // Put val and squared val to buffers
    float squared_val = val * val;
    data->comp.squared_sum += squared_val;

    ring_buf_put(&data->comp.dline, val);
    ring_buf_put(&data->comp.squared_acc, squared_val);

    if (!ring_buf_full(&data->comp.squared_acc)) {
        *pval = 0.0f;
        return;
    }

    data->comp.squared_sum -= ring_buf_get(&data->comp.squared_acc);
    float old_val = ring_buf_get(&data->comp.dline);
    float rms;
    float squared_mean = data->comp.squared_sum / data->comp.squared_acc.size;
    if (squared_mean >= 0) {
        rms = sqrt_f32(squared_mean);
    } else {
        rms = 0.0f;
    }
    // For debug
    // union {
    //     float f;
    //     uint32_t i;
    // } fuint = {rms};
    // data->flow_info._pad2 = fuint.i;  // audio RMS
    // data->flow_info._pad2 = *(uint32_t*)0x20009b88;  // MUL2 0.02f
    // data->flow_info._pad2 = *(uint32_t*)0x20009abc;  // MUL1 0.8f
    // data->flow_info._pad2 = *(uint32_t*)0x2000a184;  // FM depth mul 1

    float rms_db = lin2db(rms);

    if (rms_db == NAN) {
        rms_db = UNITY_LVL;
    }

    float gain_comp = rms_db;
    float gain_gate = rms_db;

    // Compute gains
    if (rms_db > data->comp.threshold) {
        gain_comp = data->comp.threshold + (rms_db - data->comp.threshold) / data->comp.ratio_comp;
    } else {
        gain_gate = data->comp.threshold + (rms_db - data->comp.threshold) / RATIO_GATE;
        if (rms_db - gain_gate > 40.0f){
            gain_gate = rms_db - 40.0f;
        }
    }

    // level detector
    float comp_change = rms_db - gain_comp;
    data->comp.l1_comp = MAX(comp_change, (1 - RELEASE_ALPHA) * data->comp.l1_comp + RELEASE_ALPHA * comp_change);
    float gate_change = rms_db - gain_gate;
    data->comp.l1_gate = MIN(gate_change, (1 - RELEASE_ALPHA) * data->comp.l1_gate + RELEASE_ALPHA * gate_change);

    data->comp.corr = (1 - ATT_ALPHA) * data->comp.corr + ATT_ALPHA * (data->comp.l1_gate + data->comp.l1_comp);

    float corr = -data->comp.corr + data->comp.makeup;

    old_val *= db2lin(corr);

    *pval = old_val;
    return;
}

float am_modulation(float val, float am_carrier_lvl, float am_level) {
    val *= am_level;
    val = soft_limiter(val, am_carrier_lvl);
    return val + am_carrier_lvl;
}

float fm_modulate(float val) {
    if (data->fm_demod.emphasis_on) {
        arm_biquad_cascade_df1_f32(&data->fm_demod.preemp_filter, &val, &val, 1);
        val *= 2.0f;
    }
    val *= *fm_depth_of_mod;
    data->fm_demod.phase += val;
    return data->fm_demod.phase * M_PI_F;
}


/**
 * Amplify TX IQ signal for TX output power
 */
__attribute__((noinline)) void tx_amp(float *i, float *q) {
    float k;
    switch (*modulation)
    {
        case MOD_LSB:
        case MOD_LSB_D:
        case MOD_USB:
        case MOD_USB_D:
            k = data->tx_amp_coeffs.ssb;
            break;
        case MOD_CW:
        case MOD_CWR:
            k = data->tx_amp_coeffs.cw;
            break;
        case MOD_AM:
            return;
            break;
        case MOD_NFM:
            k = data->tx_amp_coeffs.fm;
            break;
        default:
            k = 1.0f;
            break;
    }
    *i *= k;
    *q *= k;
}


/**
 * Process AM/FM RX signals
 */

// fs / (2 * pi * bw)
#define FM_LIMIT 1.591549431f
void fm_demodulate(void *S, cfloat_t *iq_sample, float *out, uint32_t _n_samples){
    UNUSED(S);
    UNUSED(_n_samples);

    cfloat_t iq_dot;
    float output;

    cfloat_t *iq_history = data->fm_demod.iq_history;


    float mag = (iq_sample->real * iq_sample->real + iq_sample->imag * iq_sample->imag);

    if (*sql) {
        data->fm_sql.iq_squared_sum += mag;
        data->fm_sql.iq_squared_cnt++;
        if (data->fm_sql.iq_squared_cnt >= 100) {
            data->fm_sql.iq_rms_db = lin2db(data->fm_sql.iq_squared_sum / 100.0f) / 2.0f;
            data->fm_sql.iq_squared_sum = 0.0f;
            data->fm_sql.iq_squared_cnt = 0;
        }
        if (data->fm_sql.iq_rms_db > (-110.0f + *sql)) {
            data->fm_sql.counter = 3000;
        } else if (data->fm_sql.counter > 0) {
            data->fm_sql.counter--;
        } else {
            *out = 0.0f;
            return;
        }
    }

    mag = sqrt_f32(mag);

    if (mag > 0.0f) {
        // save input
        iq_history[0].real = iq_sample->real / mag;
        iq_history[0].imag = iq_sample->imag / mag;
    } else {
        iq_history[0].real = 0.0f;
        iq_history[0].imag = 0.0f;
    }

    // data->fm_demod.mag_val += (mag - data->fm_demod.mag_val) * 0.1f;

    // Calculate derivative
    iq_dot.real = iq_history[0].real - iq_history[2].real;
    iq_dot.imag = iq_history[0].imag - iq_history[2].imag;
    // Calculate output (I[1] * Q') - (Q[1] * I')
    output = (iq_history[1].real * iq_dot.imag);
    output -= (iq_history[1].imag * iq_dot.real);
    // update history
    iq_history[2] = iq_history[1];
    iq_history[1] = iq_history[0];

    if (output > FM_LIMIT) {
        output = FM_LIMIT;
    } else if (output < -FM_LIMIT) {
        output = -FM_LIMIT;
    }

    // HPF for signal/noise detection
    float out_high;
    arm_biquad_cascade_df1_f32(&data->fm_demod.iir_snr_detector, &output, &out_high, 1);
    out_high *= out_high;
    data->fm_demod.hpf_env += (out_high - data->fm_demod.hpf_env) * 0.001f;

    // Output scaling factor
    float hpf_rms = sqrt_f32(data->fm_demod.hpf_env);
    // float k;
    // Noise lvl ~ 0.07f;
    // Signal lvl ~
    // if (hpf_rms > 0.07f) {
    //     // noise
    //     k = 0.02f;
    // } else {
    //     k = 0.22f;
    // }

    // Low signal ~ rms 0.13f
    // High signal ~ rms 0.10f
    float k = (0.13f - hpf_rms) / (0.13f - 0.1f);
    if (k < 0.0f) {
        k = 0.0f;
    } else if (k > 1.0f) {
        k = 1.0f;
    }
    k = 0.05f + k * 0.09f;
    // k = 0.13f;
    // if (hpf_rms > 0.11f) {
    //     k = 0.01f;
    // }
    data->fm_demod.avg_k += (k - data->fm_demod.avg_k) * 0.01f;

    float scale_factor = data->fm_demod.avg_k / FM_LIMIT;

    output = output * scale_factor;

    if (data->fm_demod.emphasis_on) {
        // De-emphasis
        arm_biquad_cascade_df1_f32(&data->fm_demod.deemp_filter, &output, out, 1);
    } else {
        *out = output * 2.5f;
    }
}

/**
 * Process AM/FM rx signal after demodulation
 */
void am_fm_rx_process() {
    float *demod = (float*) AM_FM_DEMOD;

    float val = *demod;
    switch (*modulation)
    {
        case MOD_AM:
            // remove DC offset
            val = dc_blocker(val, (1.0f - RX_AM_DC_BLOCKER_ALPHA), &data->rx_am_dc_blocker);
            break;
        case MOD_NFM:
            // remove DC offset
            val = dc_blocker(val, (1.0f - RX_FM_DC_BLOCKER_ALPHA), &data->rx_fm_dc_blocker);
            break;
        default:
            break;
    }
    *demod = val;
}


/**
 * Adaptive notch filter
 */


void anf_update(void) {
    arm_biquad_casd_df1_inst_f32 *flt = (arm_biquad_casd_df1_inst_f32 *)ARM_BIQUAD_CASD_DF1_INST_VALUE;
    const float k = 3e-3f;

    if (data->anf.enabled) {
        float mean_squared = flt->pState[0] * flt->pState[0] + flt->pState[1] * flt->pState[1];
        data->anf.mean_squared += (mean_squared - data->anf.mean_squared) * 0.01f;

        // float width = 100.0f; // Hz
        const float gain = 0.9754784f;
        const float r = 0.974862f;

        if (!isfinite(flt->pState[2]) || !isfinite(flt->pState[3])) {
            flt->pState[0] = 0.0f;
            flt->pState[1] = 0.0f;
            flt->pState[2] = 0.0f;
            flt->pState[3] = 0.0f;
        }

        // estimate_an
        float an = data->anf.an;
        float gradient = flt->pState[2] * (gain * flt->pState[3] - flt->pState[1]);
        an = an - k * gradient / (data->anf.mean_squared + 1e-12f);

        an = MIN(1.9597101f, an);  // 400 Hz
        an = MAX(-0.85155857f, an);  // 4000 Hz

        data->anf.an = an;

        // update_coeffs
        float *coeffs = (float *)flt->pCoeffs;
        coeffs[1] = -an * gain;
        coeffs[3] = -flt->pCoeffs[1];

        if (coeffs[0] != gain) {
            coeffs[0] = gain;
            coeffs[2] = gain;
            coeffs[4] = -r * gain;
        }
    }
}


/**
 * Custom cmd parser
 */


static void process_custom_cmd() {
    union {
        uint32_t i;
        float32_t f;
    } fuint;

    if (i2c_regs[x6100_flow_fm_emp] != data->i2c_raw.flow_fm_emp.i) {
        data->i2c_raw.flow_fm_emp.i = i2c_regs[x6100_flow_fm_emp];
        if (data->flow.fmt != data->i2c_raw.flow_fm_emp.v.flow_fp16) {
            set_flow_params(data->i2c_raw.flow_fm_emp.v.flow_fp16);
        }
        data->fm_demod.emphasis_on = data->i2c_raw.flow_fm_emp.v.fm_emp;
    }

    if (i2c_regs[x6100_dac_adc_offsets] != data->i2c_raw.dac_adc_offsets.i) {
        data->i2c_raw.dac_adc_offsets.i = i2c_regs[x6100_dac_adc_offsets];

        fuint.i = data->i2c_raw.dac_adc_offsets.v.adc_dac_gain_offset << 16;
        data->tx_amp_coeffs.adc_dac_gain_offset = fuint.f;
        fuint.i = data->i2c_raw.dac_adc_offsets.v.dac_gain_offset << 16;
        data->tx_amp_coeffs.dac_gain_offset = fuint.f;
        data->tx_amp_coeffs.outdated = true;
    }

    if (i2c_regs[x6100_tx_filter] != data->i2c_raw.tx_filters.i) {
        data->i2c_raw.tx_filters.i = i2c_regs[x6100_tx_filter];
        uint16_t low = data->i2c_raw.tx_filters.v.low;
        uint16_t high = data->i2c_raw.tx_filters.v.high;
        if ((low == 0) && (high == 0)) {
            low = 160;
            high = 3000;
        }
        update_tx_filter_params(low, high);
    }
}

void process_i2c_cmd(void) {
    process_custom_cmd();

    // COMP
    if ((i2c_regs[x6100_cmplevel_cmpe] != data->i2c_raw.cmplevel_cmpe) || (data->comp.ratio_comp == 0.0f)) {
        data->i2c_raw.cmplevel_cmpe = i2c_regs[x6100_cmplevel_cmpe];
        float threshold_offset = (int8_t)((data->i2c_raw.cmplevel_cmpe >> 3) & 0xFC) * 0.125f;
        float makeup_offset = (int8_t)((data->i2c_raw.cmplevel_cmpe >> 9) & 0xFC) * 0.125f;

        // ratio configured with -2 offset: 0 -> 2:1, 1 -> 3:1, etc
        data->comp.ratio_comp = *cmp_level + 2.0f;
        data->comp.threshold = TH_COMP + threshold_offset;
        data->comp.makeup = ((UNITY_LVL - data->comp.threshold) * (1.0f - 1.0f / data->comp.ratio_comp) - 2.0f) + makeup_offset;
    }

    // ANF
    if (i2c_regs[x6100_dnfcnt_dnfwidth_dnfe] != data->i2c_raw.dnfcnt_dnfwidth_dnfe) {
        data->i2c_raw.dnfcnt_dnfwidth_dnfe = i2c_regs[x6100_dnfcnt_dnfwidth_dnfe];
        data->anf.enabled = (data->i2c_raw.dnfcnt_dnfwidth_dnfe >> 25) & 1;
    }

    // Update power-related coefficients, if power was changed
    if (i2c_regs[x6100_rfg_txpwr] != data->i2c_raw.rfg_txpwr) {
        data->i2c_raw.rfg_txpwr = i2c_regs[x6100_rfg_txpwr];
        data->tx_amp_coeffs.outdated = true;

        // fft dec
        uint8_t fft_dec = (data->i2c_raw.rfg_txpwr >> 16) & 0xf;
        fft_dec = fft_dec > 3 ? 3 : fft_dec;
        if (data->flow.fft_dec != fft_dec) {
            flow_collecting_reset();
            data->flow.fft_dec = fft_dec;
            data->flow_info.fft_dec = fft_dec;
        }
    }
    if (i2c_regs[x6100_if_shift] != data->i2c_raw.if_shift) {
        data->i2c_raw.if_shift = i2c_regs[x6100_if_shift];
        data->if_shift.on = !((data->i2c_raw.if_shift >> 24) & 0xFF);
        data->if_shift.freq = (int32_t)(data->i2c_raw.if_shift << 8) >> 8;
        data->if_shift.step = data->if_shift.freq * M_TWOPI_F / 100000.0f;
    }
}
