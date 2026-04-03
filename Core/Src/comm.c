#include "comm.h"

#include "external.h"
#include "stdio.h"
#include "stdbool.h"
#include "compressor.h"
#include "iirdecim.h"
#include "if_shift.h"
#include "modulations.h"
#include "anf.h"

static uint8_t *cmp_level = (uint8_t *)CMP_LEVEL_VALUE;


struct {
    uint32_t rfg_txpwr;
    uint32_t cmplevel_cmpe;
    uint32_t dnfcnt_dnfwidth_dnfe;
    uint32_t if_shift;
    x6100_reg_vox_t vox;
    x6100_reg_flow_fmt_fm_emp_t flow_fm_emp;
    x6100_reg_dac_adc_offsets_t dac_adc_offsets;
    x6100_reg_tx_filter_t tx_filters;
} i2c_raw __attribute((section(".ccmram")));

struct
{
    x6100_flow_fmt_t fmt;
    uint8_t fft_dec;
    flow_info_t info;
    uint8_t data[CFLOAT32_BYTES * FLOW_SEQ_SAMPLES * 2]; // Buffer with max allowed size.
    uint8_t *write_p;
    uint8_t *read_p;
    uint8_t sample_size;      // Size of dingle sample in bytes
    uint16_t desired_samples; // Count of desired samples to collect
    struct
    {
        uint32_t sum;
        uint8_t cnt;
    } avg_freq;
} flow __attribute((section(".ccmram")));

// UART flow fields
static uint32_t *flow_reserved_3 = (uint32_t*)FLOW_RESERVED_ADDR;
static uint8_t *stop_copy_flow = (uint8_t *)STOP_COPY_FLOW;
static uint32_t *flow_n_samples = (uint32_t *)FLOW_N_SAMPLES; // Count of samples to send in a block, 512
static uint32_t *flow_samples_counter = (uint32_t *)FLOW_SAMPLES_COUNTER;  // Count of already collected samples in OEM function
static float *flow_samples_cplx = (float *)FLOW_SAMPLES_CPLX;  // Pointer to array of flow samples


static inline void update_tx_filter_params(uint16_t low, uint16_t high) {
    float rate = *(float *)TX_SAMPLING_RATE_12_5;
    void *flt_S = (void *)BIQUAD_TX_FLT_INST;
    setup_biquad_filter(rate, low, high, flt_S, 1);
}

static inline void flow_collecting_reset(void) {
    // Reset collecting to prevent sending
    *stop_copy_flow = false;
    *flow_samples_counter = 0;
    // reset flow_data pointers
    flow.write_p = flow.data;
    flow.read_p = flow.data;
    flow.avg_freq.cnt = 0;
    flow.avg_freq.sum = 0;
    iirdecim_reset();
}

static inline void *copy_flow_f32(float *pSrc, float *pDst, size_t n_src_cmplx_samples) {
    iir_decim_iq_n(flow.fft_dec, pSrc, pDst, n_src_cmplx_samples);
    n_src_cmplx_samples >>= flow.fft_dec;

    return pDst + (n_src_cmplx_samples << 1);
}

static inline void *copy_flow_b16(float *pSrc, void *pDst, size_t n_src_cmplx_samples) {
    float *tmp;
    size_t n_blocks;
    iir_decim_iq_n(flow.fft_dec, pSrc, pSrc, n_src_cmplx_samples);
    n_src_cmplx_samples >>= flow.fft_dec;
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

void comm_init(void)
{
    flow.fmt = x6100_flow_fp32;
    flow.fft_dec = 0;

    flow.info.fft_dec = flow.fft_dec;
    flow.info.flow_fmt = flow.fmt;
    flow.info.flow_seq_n = 0xf;
    flow.info.flow_seq_total = 1;
    flow.info.lo_freq = 0;
    flow.info.vary_freq = false;
    flow.info._pad1 = 0;
    flow.info._pad2 = 0;

    flow.write_p = flow.data;
    flow.read_p = flow.data;
    flow.sample_size = CFLOAT32_BYTES;
    flow.desired_samples = FLOW_SEQ_SAMPLES;
    flow.avg_freq.sum = 0;
    flow.avg_freq.cnt = 0;

    // init fft_dec instances
    iirdecim_init();

    i2c_raw.tx_filters.v.low = 160;
    i2c_raw.tx_filters.v.high = 3000;
}

void set_flow_params(x6100_flow_fmt_t fmt)
{
    uint16_t size;
    flow.fmt = fmt;
    switch (fmt) {
        case x6100_flow_fp32:
            flow.sample_size = CFLOAT32_BYTES;
            size = FLOW_SEQ_SAMPLES;
            break;
            case x6100_flow_bf16:
            flow.sample_size = CFLOAT16_BYTES;
            size = FLOW_SEQ_SAMPLES * 2;
            break;

        default:
            return;
    }
    size_t block_size = FLOW_SEQ_SAMPLES * CFLOAT32_BYTES / flow.sample_size;
    flow.info.flow_seq_total = size / block_size;
    flow.desired_samples = size;
    flow_collecting_reset();
}

// Patched version of copying flow data

uint32_t copy_flow(float *p_Dst) {
    uint32_t copied = 0;
    uint32_t *custom_flow_reserved_3 = (uint32_t *)&flow.info;
    uint32_t avg_freq;

    flow.info.flow_seq_n = 0xf;
    // flow.info.pad = TIM2->ARR;
    // TIM2->ARR = 4000 / 5;

    // TIM2->PSC = 80;
    // TIM2->ARR = 16000;
    // TIM2->EGR = TIM_EGR_UG;
    // // Force auto reloading
    // TIM2->CR1 |= TIM_CR1_ARPE;
    // if ((flow.read_p == flow.data) && (TIM2->PSC != 20)) {
    //     TIM2->PSC = 20;
    // } else {
    //     TIM2->PSC = 5;
    // }
    // TIM2->SR
    if (flow.data + (flow.desired_samples * flow.sample_size) <= flow.write_p) {
        ext_arm_copy_f32((float *)flow.read_p, p_Dst, (*flow_n_samples) << 1);
        uint32_t copied_bytes = (*flow_n_samples * 8);
        flow.info.flow_seq_n = (flow.read_p - flow.data) / copied_bytes;
        avg_freq = flow.avg_freq.sum / flow.avg_freq.cnt;
        if (avg_freq != flow.info.lo_freq) {
            flow.info.lo_freq = avg_freq;
            flow.info.vary_freq = true;
        } else {
            flow.info.vary_freq = false;
        }
        flow_reserved_3[0] = custom_flow_reserved_3[0];
        flow_reserved_3[1] = custom_flow_reserved_3[1];
        flow_reserved_3[2] = custom_flow_reserved_3[2];
        flow.read_p += copied_bytes;
        copied = 1;
        if (flow.read_p >= flow.write_p) {
            // All data sent, reset counters
            flow_collecting_reset();
        }
  }
  return copied;
}


void flow_collecting_at_end(void) {
    USE_OEM_FREQ_PLUS_RIT_AS(freq_plus_rit);
    size_t remain_samples = (flow.data + (flow.desired_samples * flow.sample_size) - flow.write_p) / flow.sample_size;
    // Collect data.
    if (remain_samples > 0) {
        // Add prev freq to avg acc
        flow.avg_freq.sum += *freq_plus_rit;
        flow.avg_freq.cnt += 1;
        if (flow.write_p == flow.data) {
            // Remember freq and fmt at filling 1st block
            flow.info.flow_fmt = flow.fmt;
            flow.info.lo_freq = *freq_plus_rit;
            iirdecim_reset();
        }

        ssize_t offset = -1;
        if (remain_samples > (*flow_n_samples >> flow.fft_dec)) {
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
            // flow.write_p = copy_flow_f32(flow_samples_cplx + (offset << 1), (float *)flow.write_p, 128);
            if (flow.fmt == x6100_flow_fp32) {
                flow.write_p = copy_flow_f32(flow_samples_cplx + (offset << 1), (float *)flow.write_p, 128);
            } else {
                flow.write_p = copy_flow_b16(flow_samples_cplx + (offset << 1), (void *)flow.write_p, 128);
            }
        }
    }
}


/**
 * Custom cmd parser
 */


static void process_custom_cmd() {
    USE_OEM_I2C_REGS_AS(i2c_regs);

    union {
        uint32_t i;
        float32_t f;
    } fuint;

    if (i2c_regs[x6100_flow_fm_emp] != i2c_raw.flow_fm_emp.i) {
        i2c_raw.flow_fm_emp.i = i2c_regs[x6100_flow_fm_emp];
        if (flow.fmt != i2c_raw.flow_fm_emp.v.flow_fp16) {
            set_flow_params(i2c_raw.flow_fm_emp.v.flow_fp16);
        }
        set_fm_demod_emphasis(i2c_raw.flow_fm_emp.v.fm_emp);
    }

    if (i2c_regs[x6100_dac_adc_offsets] != i2c_raw.dac_adc_offsets.i) {
        i2c_raw.dac_adc_offsets.i = i2c_regs[x6100_dac_adc_offsets];

        fuint.i = i2c_raw.dac_adc_offsets.v.adc_dac_gain_offset << 16;
        float adc_dac_gain_offset = fuint.f;
        fuint.i = i2c_raw.dac_adc_offsets.v.dac_gain_offset << 16;
        float dac_gain_offset = fuint.f;
        set_adc_dac_gain_offsets(adc_dac_gain_offset, dac_gain_offset);
    }

    if (i2c_regs[x6100_tx_filter] != i2c_raw.tx_filters.i) {
        i2c_raw.tx_filters.i = i2c_regs[x6100_tx_filter];
        uint16_t low = i2c_raw.tx_filters.v.low;
        uint16_t high = i2c_raw.tx_filters.v.high;
        if ((low == 0) && (high == 0)) {
            low = 160;
            high = 3000;
        }
        update_tx_filter_params(low, high);
    }
}

void process_i2c_cmd(void) {
    USE_OEM_I2C_REGS_AS(i2c_regs);

    process_custom_cmd();

    // COMP
    if (i2c_regs[x6100_cmplevel_cmpe] != i2c_raw.cmplevel_cmpe) {
        i2c_raw.cmplevel_cmpe = i2c_regs[x6100_cmplevel_cmpe];
        float threshold_offset = (int8_t)((i2c_raw.cmplevel_cmpe >> 3) & 0xFC) * 0.125f;
        float makeup_offset = (int8_t)((i2c_raw.cmplevel_cmpe >> 9) & 0xFC) * 0.125f;

        // ratio configured with -2 offset: 0 -> 2:1, 1 -> 3:1, etc
        set_comp_ratio(*cmp_level + 2.0f);
        set_comp_threshold_offset(threshold_offset);
        set_comp_makeup_offset(makeup_offset);
    }

    // ANF
    if (i2c_regs[x6100_dnfcnt_dnfwidth_dnfe] != i2c_raw.dnfcnt_dnfwidth_dnfe) {
        i2c_raw.dnfcnt_dnfwidth_dnfe = i2c_regs[x6100_dnfcnt_dnfwidth_dnfe];
        set_anf_enable((i2c_raw.dnfcnt_dnfwidth_dnfe >> 25) & 1);
    }

    // Update power-related coefficients, if power was changed
    if (i2c_regs[x6100_rfg_txpwr] != i2c_raw.rfg_txpwr) {
        i2c_raw.rfg_txpwr = i2c_regs[x6100_rfg_txpwr];
        float pwr = (uint8_t)(i2c_raw.rfg_txpwr >> 8) * 0.1f;
        set_pwr(pwr);

        // fft dec
        uint8_t fft_dec = (i2c_raw.rfg_txpwr >> 16) & 0xf;
        fft_dec = fft_dec > 3 ? 3 : fft_dec;
        if (flow.fft_dec != fft_dec) {
            flow_collecting_reset();
            flow.fft_dec = fft_dec;
            flow.info.fft_dec = fft_dec;
        }
    }
    if (i2c_regs[x6100_if_shift] != i2c_raw.if_shift) {
        i2c_raw.if_shift = i2c_regs[x6100_if_shift];
        bool on = !((i2c_raw.if_shift >> 24) & 0xFF);
        int32_t freq = (int32_t)(i2c_raw.if_shift << 8) >> 8;
        if_shift_setup(on, freq);

        // uint32_t *tx_flags = (uint32_t*)0x200001b8;
        // if (data->if_shift.on) {
        //     *tx_flags |= 0x20;
        // } else {
        //     *tx_flags &= (~0x20);
        // }
    }
}
