#include "compressor.h"

#include <dsp/controller_functions.h>

#include "offsets.h"

#include "noise_reduction.h"

#include "math.h"
#include "stdbool.h"
#include "stdarg.h"
#include "stdio.h"
#include "ring_buf.h"
#include "modulations.h"
#include "anf.h"
#include "comm.h"
#include "if_shift.h"
#include "external.h"
// #include <dsp/fast_math_functions.h>
// #include <dsp/support_functions.h>


// #define MAX(a, b) (a > b ? a : b)
// #define MIN(a, b) (a < b ? a : b)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

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
 * General constants
 */

#define SWR_SCAN 0x00010



/**
 * DC blockers constants
 */

#define TX_DC_BLOCKER_ALPHA (0.05f)
#define RX_AM_DC_BLOCKER_ALPHA (0.03f)
#define RX_FM_DC_BLOCKER_ALPHA (0.01f)




enum __attribute__((__packed__)) rx_tx_process_state {
    RX_TX_STATE_NORMAL,
    RX_TX_STATE_PIN_DISCHARGE,
    RX_TX_STATE_MUTE,
    RX_TX_STATE_WAIT_PIN_SWITCH,
};


/**
 * State struct
 */

typedef struct {
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

        // multiplier for output (before DAC)
        float dac_output_coeff;
        // multiplier for input (after ADC)
        float adc_input_coeff;

        float pwr;
        bool outdated;
    } tx_amp_coeffs;

    /* AM/FM RX processing data */
    // rx am/fm DC blocker
    struct dc_blocker_t rx_am_dc_blocker;
    struct dc_blocker_t rx_fm_dc_blocker;


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
        enum mod_t mode;
        uint32_t att;
        uint32_t pre;
        uint8_t tx;
    } prev_state;
    bool swr_scan;
    uint8_t band_id;

    /* Debug and pad */
    // tone

} data_t;
// } __attribute__ ((aligned (16))) data_t;

data_t data_arr __attribute((section(".ccmram")));

static data_t *data = &data_arr;



/**
 * Actual values pointers
 */

// Compressor values
static uint8_t *cmp_enabled = (uint8_t *)CMP_ENABLED_VALUE;


static float *am_carrier_lvl = (float *)AM_CARRIER_LEVEL_VALUE;

inline static void fast_iq_offset_counter_setup() {
    USE_OEM_TX_FLAG_AS(pTx);

    int32_t *input_data = *(int32_t **)INPUT_RF_SIGNAL_INT_ADDR;
    uint32_t samples_count = *(uint32_t*)SAMPLES_COUNT_VALUE;

    // Increase counter
    if (data->rx_iq_corr.step <= 800) {
        data->rx_iq_corr.step++;
    }
    if (*pTx != data->rx_iq_corr.last_tx) {
        data->rx_iq_corr.last_tx = *pTx;
        if (!*pTx) {
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

    data->comp.ratio_comp = 2.0f; // Default value is 2:1

    set_flow_params(x6100_flow_fp32);

    if_shift_init();

    fm_demod_init();
    comm_init();

    // Noise reduction init
    nr_init();
}


/**
 * Update signal processing parameters. Inject, calls at beginning of the DMA process.
 */

static void reset_filters_states_on_changes() {
    USE_OEM_MODULATION_AS(pMode);

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

    bool clear = false;
    uint32_t *decim2_i = (uint32_t*)FIR_DECIM_2_I;

    if (*pMode != data->prev_state.mode) {
        data->prev_state.mode = *pMode;
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
        // interp
        ext_arm_fill_f32(0.0f, (float*)FIR_INTERP_8_STATE, 8);
        ext_arm_fill_f32(0.0f, (float*)FIR_INTERP_8_BUF, 8);

        // decim_2
        ext_arm_fill_f32(0.0f, (float*)FIR_DECIM_2_BUF, 2);
        ext_arm_fill_f32(0.0f, (float*)FIR_DECIM_2_STATE, 0x25);

        *decim2_i = 0;
    }
}

void configure(void) {
    USE_OEM_TX_FLAG_AS(pTx);
    USE_OEM_I2C_REGS_AS(i2c_regs);

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
        tx_coeff_calc(data->tx_amp_coeffs.pwr);
    }
    fast_iq_offset_counter_setup();

    // Reset ring buffers (for compressor) on RX/TX state change
    if (data->prev_state.tx != *pTx) {
        data->prev_state.tx = *pTx;
        if (*pTx) {
            ring_buf_reset(&data->comp.dline);
        }
    }
    nr_setup_filters();

    // bool *gpiof = (bool*)0x200001fa;
    // bool *gpiof2 = (bool*)0x2000020e;
    // bool *gpioa = (bool*)0x20000222;
    // bool *gpiod = (bool*)0x2000024a;
    // data->flow_info._pad2 = (*gpiof) | (*gpiof2 << 8) | (*gpioa << 16) | (*gpiod << 24);
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
 * Set IQ scale on changing TX power
 */
void tx_coeff_calc(float pwr) {
    USE_OEM_FM_DEPTH_OF_MOD_AS(fm_depth_of_mod);

    data->tx_amp_coeffs.outdated = false;
    float *am_depth_of_mod = (float *)AM_DEPTH_OF_MOD_VALUE;
    float k;

    // flow_reserved_3[0] = data->band_id;
    // flow_reserved_3[1] = *(uint32_t*)&adc_dac_gain_offset;
    // flow_reserved_3[2] = *(uint32_t*)&dac_band_gain_offset;

    data->tx_amp_coeffs.dac_output_coeff = db2lin(data->tx_amp_coeffs.adc_dac_gain_offset + data->tx_amp_coeffs.dac_gain_offset);
    data->tx_amp_coeffs.adc_input_coeff = 1.0f / db2lin(data->tx_amp_coeffs.adc_dac_gain_offset);
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
    k *= data->tx_amp_coeffs.dac_output_coeff;

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
    *fm_depth_of_mod = 32.5f * data->tx_amp_coeffs.adc_input_coeff;
}

/**
 * Compressor, limiter, gains
 */
void set_adc_dac_gain_offsets(float adc_dac_gain_offset, float dac_gain_offset)
{
    data->tx_amp_coeffs.adc_dac_gain_offset = adc_dac_gain_offset;
    data->tx_amp_coeffs.dac_gain_offset = dac_gain_offset;
    data->tx_amp_coeffs.outdated = true;
}

void set_pwr(float pwr)
{
    data->tx_amp_coeffs.pwr = pwr;
    data->tx_amp_coeffs.outdated = true;
}

void set_comp_ratio(float val)
{
    data->comp.ratio_comp = val;
}

void set_comp_threshold_offset(float val)
{
    data->comp.threshold = TH_COMP + val;
}

void set_comp_makeup_offset(float val)
{
    data->comp.makeup = ((UNITY_LVL - data->comp.threshold) * (1.0f - 1.0f / data->comp.ratio_comp) - 2.0f) + val;
}

void compress(float *pval)
{
    USE_OEM_MODULATION_AS(pMode);
    switch (*pMode) {
        // case MOD_LSB:
        case MOD_LSB_D: // lsb-d
        case MOD_USB_D: // usb-d
            *pval *= data->tx_amp_coeffs.adc_input_coeff;
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
    val *= data->tx_amp_coeffs.adc_input_coeff;

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

/**
 * Amplify TX IQ signal for TX output power
 */
__attribute__((noinline)) void tx_amp(float *i, float *q) {
    USE_OEM_MODULATION_AS(pMode);
    float k;
    switch (*pMode)
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
 * Process AM/FM rx signal after demodulation
 */
void am_fm_rx_process() {
    USE_OEM_MODULATION_AS(pMode);

    float *demod = (float*) AM_FM_DEMOD;

    float val = *demod;
    switch (*pMode)
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
