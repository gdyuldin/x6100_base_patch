#include "compressor.h"

#include "math.h"
#include "stdbool.h"
#include "log10f.c"
#include "powf.c"
#include "sin_values.c"
#include "stdarg.h"
#include "stdio.h"

// #define PER_BAND_OUT_POWER


#define MAX(a, b) (a > b ? a : b)
#define MIN(a, b) (a < b ? a : b)

/**
 * Genera constants
 */

#define SWR_SCAN 0x00010

#define BAND_160 1
#define BAND_80 3
#define BAND_60 5
#define BAND_40 7
#define BAND_30 9
#define BAND_20 11
#define BAND_17 13
#define BAND_15 15
#define BAND_12 17
#define BAND_10 19
#define BAND_6 21


/**
 * Compressor/gate constants
 */

 // Sine signal level (zero to peak) on SSB, which produces expected power
#define UNITY_LVL -42.0f

// Level of microphone noise
#define NOISE_LVL -80.0f

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
 * Limir constants
 */
#define LIMITER_MAX_VAL 0.00741f


/**
 * I2C registers
 */
typedef enum
{
    x6100_sple_atue_trx = 12,
    x6100_vi_vm,
    x6100_rfg_txpwr = 15,
    x6100_dnfcnt_dnfwidth_dnfe = 24,
    x6100_cmplevel_cmpe = 25,
} x6100_cmd_enum_t;


enum __attribute__((__packed__)) mod_t {
    MOD_LSB,
    MOD_LSB_D,
    MOD_USB,
    MOD_USB_D,
    MOD_CW,
    MOD_CWR,
    MOD_AM,
    MOD_NFM,
};

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

        uint32_t i2c_reg_val;
    } comp;

    struct dc_blocker_t tx_dc_blocker;

    struct {
        /* TX level control data */
        // tx amp coeffs
        float ssb;
        float cw;
        float am;
        float fm;

        uint32_t i2c_pwr_reg;
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

        uint32_t i2c_reg_val;
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
    enum mod_t prev_modulation;
    uint8_t tx_flag;
    bool swr_scan;
    uint8_t band_id;
    /* Debug and pad */
    // tone
    uint32_t tone_step;

} __attribute__ ((aligned (16))) data_t;

#define DATA_P (0x20030000 - sizeof(data_t))

static data_t *data = (data_t*)DATA_P;

/**
 * Output power per-band correction values
 */
__section(".rodata.tx_coeffs_corr_table") const float tx_coeffs_corr_table[23] =
    {
        [BAND_160] = 1.0f,
        [BAND_80] = -0.2f,
        [BAND_40] = -0.4f,
        [BAND_30] = -0.4f,
        [BAND_20] = 0.6f,
        [BAND_17] = -0.0f,
        [BAND_15] = 2.0f,
        [BAND_12] = 0.8f,
        [BAND_10] = 2.0f,
        [BAND_6] = -0.0f,
};


/**
 * Actual values pointers
 */

// Modulation pointer
static enum mod_t *modulation = (enum mod_t *)0x2000a8cd;

// Compressor values
static uint8_t *cmp_enabled = (uint8_t *)0x200000c4;
static uint8_t *cmp_level = (uint8_t *)0x200000c3;

// Squelch value
static uint8_t *sql = (uint8_t *)0x200000a9;

// UART flow fields
static uint32_t *flow_reserved_3 = (uint32_t*)0x200013f8;


// I2C registers values start pointer
static uint32_t *i2c_regs = (uint32_t *)0x2000357c;

static volatile uint8_t *tx_flag = (uint8_t *)0x2000a8cf;

static float *am_carrier_lvl = (float *)0x2000a174;


/**
 * Declarations of compiled in OEM functions
 */
extern void arm_fill_f32 (float val, float* data, uint32_t size) __attribute__((noinline, section(".arm_fill_f32_sec")));

/**
 * Db <-> linear conversion
 */

inline float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline float lin2db(float val) {
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

float soft_limiter(float val) {
    const float th = LIMITER_MAX_VAL * 0.5f;
    float x;
    if (val > th) {
        x = th  / val;
        val = (1.0f - x) * (LIMITER_MAX_VAL - th) + th;
    } else if (val < -th) {
        x = -th  / val;
        val = -((1.0f - x) * (LIMITER_MAX_VAL - th) + th);
    }
    return val;
}


static inline void update_comp_params() {
    if ((i2c_regs[x6100_cmplevel_cmpe] != data->comp.i2c_reg_val) || (data->comp.ratio_comp == 0.0f)) {
        data->comp.i2c_reg_val = i2c_regs[x6100_cmplevel_cmpe];
        float threshold_offset = (int8_t)((data->comp.i2c_reg_val >> 3) & 0xFC) * 0.125f;
        float makeup_offset = (int8_t)((data->comp.i2c_reg_val >> 9) & 0xFC) * 0.125f;

        // ratio configured with -2 offset: 0 -> 2:1, 1 -> 3:1, etc
        data->comp.ratio_comp = *cmp_level + 2.0f;
        data->comp.threshold = TH_COMP + threshold_offset;
        data->comp.makeup = ((UNITY_LVL - data->comp.threshold) * (1.0f - 1.0f / data->comp.ratio_comp) - 2.0f) + makeup_offset;
    }
}

void update_anf_params() {
    if (i2c_regs[x6100_dnfcnt_dnfwidth_dnfe] != data->anf.i2c_reg_val) {
        data->anf.i2c_reg_val = i2c_regs[x6100_dnfcnt_dnfwidth_dnfe];
        data->anf.enabled = (data->anf.i2c_reg_val >> 25) & 1;
    }
}

inline static void fast_iq_offset_counter_setup() {
    int32_t *input_data = *(int32_t **)0x200003b0;
    // int32_t *input_data = (int32_t *)0x20003e94;
    uint32_t samples_count = *(uint32_t*)0x2000dd58;

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

/**
 * Setup data storage. Inject, calls at startup
 */
__attribute__((optimize("O1"))) void init_data(void) {
    uint32_t *area_end = (uint32_t*)0x20030000;
    uint32_t *area_start = area_end - sizeof(data_t) / sizeof(uint32_t);

    for (;area_start < area_end; area_start++) {
        *area_start = 0;
    }
    data->comp.dline.data = data->comp.dline_data;
    data->comp.dline.size = sizeof(data->comp.dline_data) / sizeof(*data->comp.dline_data);
    data->comp.squared_acc.data = data->comp.squared_acc_data;
    data->comp.squared_acc.size = sizeof(data->comp.squared_acc_data) / sizeof(*data->comp.squared_acc_data);
}


/**
 * Update signal processing parameters. Inject, calls at beginning of the DMA process.
 */

void configure() {
    update_comp_params();
    update_anf_params();
    bool update_tx_coeffs = false;
    // Update power-related coefficients, if power was changed
    if (i2c_regs[x6100_rfg_txpwr] != data->tx_amp_coeffs.i2c_pwr_reg) {
        data->tx_amp_coeffs.i2c_pwr_reg = i2c_regs[x6100_rfg_txpwr];
        update_tx_coeffs = true;
    }
    bool swr_scan = i2c_regs[x6100_sple_atue_trx] & SWR_SCAN;
    if (swr_scan != data->swr_scan) {
        data->swr_scan = swr_scan;
        update_tx_coeffs = true;
    }
    uint8_t band_id = (uint8_t)(i2c_regs[x6100_vi_vm] >> 8);
    if (band_id != data->band_id) {
        data->band_id = band_id;
        update_tx_coeffs = true;
    }
    if (update_tx_coeffs) {
        float pwr = (uint8_t)(data->tx_amp_coeffs.i2c_pwr_reg >> 8) * 0.1f;
        tx_coeff_calc(pwr);
    }
    fast_iq_offset_counter_setup();
    // Output audio input data
    // int32_t *tx_data = (int32_t *)0x20003a94;
    // data->tone_step++;
    // if (data->tone_step >= 50) {
    //     // Print gpio values
    //     // flow_reserved_3[0] = GPIOD->IDR;
    //     // flow_reserved_3[1] = GPIOE->IDR;
    //     // flow_reserved_3[2] = GPIOI->IDR;
    //     data->tone_step = 0;
    // }
    // Reset ring buffers (for compressor) on RX/TX state change
    if (data->tx_flag != *tx_flag) {
        data->tx_flag = *tx_flag;
        if (data->tx_flag) {
            ring_buf_reset(&data->comp.dline);
        }
    }
}

/**
 * Convert int IQ to float and apply offsets
 */
void apply_rx_iq_offset(void) {
    // IQ incoming data manipulation

    // get stack pointer
    register void *sp asm ("sp");

    float *i = (float *)sp + 0;
    float *q = (float *)sp + 1;

    float a = 0.0001f;

    if (data->rx_iq_corr.state == RX_TX_STATE_PIN_DISCHARGE) {
        // Discharge step
        const float k = 1.0f - 0.000051f;
        data->rx_iq_corr.i_mean = (data->rx_iq_corr.i_mean - data->rx_iq_corr.i_mean_fix)*k + data->rx_iq_corr.i_mean_fix;
        data->rx_iq_corr.q_mean = (data->rx_iq_corr.q_mean - data->rx_iq_corr.q_mean_fix)*k + data->rx_iq_corr.q_mean_fix;
        a = 0.0003f;
    }

    if ((data->rx_iq_corr.state == RX_TX_STATE_MUTE) || (data->rx_iq_corr.state == RX_TX_STATE_WAIT_PIN_SWITCH)) {
        *i = 0.0f;
        *q = 0.0f;
    } else {
        float i_corr = *i - data->rx_iq_corr.i_mean;
        float q_corr = *q - data->rx_iq_corr.q_mean;

        *i = i_corr;
        *q = q_corr;

        data->rx_iq_corr.i_mean += *i * a;
        data->rx_iq_corr.q_mean += *q * a;
    }
}


/**
 * Set IQ scale on changing TX power
 */
__noinline void tx_coeff_calc(float pwr) {
    float *am_depth_of_mod = (float *)0x2000a178;
    float *fm_depth_of_mod = (float *)0x2000a184;
    float k;
    float dac_gain_offset = (int8_t)(i2c_regs[x6100_rfg_txpwr] >> 16) * 0.2f;
#ifdef PER_BAND_OUT_POWER
    float dac_band_gain_offset = tx_coeffs_corr_table[data->band_id];
#else
    float dac_band_gain_offset = 0.0f;
#endif

    // flow_reserved_3[0] = data->band_id;
    // flow_reserved_3[1] = *(uint32_t*)&dac_gain_offset;
    // flow_reserved_3[2] = *(uint32_t*)&dac_band_gain_offset;

    data->dac_output_coeff = db2lin(dac_gain_offset + dac_band_gain_offset);
    data->adc_input_coeff = 1 / db2lin(dac_gain_offset);
    if (pwr >= 0) {
        float pow_scale = pwr / 10.0f;
        if (pow_scale <= 0.0f) {
            k = 1.0f;
        } else {
            k = sqrtf(pow_scale);
        }
    } else {
        k = 1.0f;
    }
    // set coeffs
    k *= data->dac_output_coeff;
    // calibrate FM to 10W with 0 gain offset
    data->tx_amp_coeffs.fm = 8.12e-2f * k;

    // AM carrier ~= fm / 2 ** 0.5 / 2
    // 25 % of output power is a carrier
    // 6W (7w wo limiter) output with unity input sine 1000 Hz. Will add 1.291 scale for both carrier and signal
    // float am_k = 1.291f;
    // float am_k = 1.195f;
    float am_k = 1.0f;
    if (data->swr_scan) {
        *am_carrier_lvl = 0.75f;
    } else {
        *am_carrier_lvl = 0.0293f * am_k * k;
    }
    *am_depth_of_mod = 3.73f * am_k * k;

    data->tx_amp_coeffs.ssb = k;
    // data->tx_amp_coeffs.fm = 7.6e-2f * k;
    data->tx_amp_coeffs.cw = 6.04e-2f * k;

    // for 2.5 w carrier at 10w output
    // *am_carrier_lvl = 0.025f * k;
    // *am_depth_of_mod = 3.25f * k;

    // for 5W carrier at 10W output
    // *am_carrier_lvl = 0.0353f * k;

    // Set FM depth of mod
    *fm_depth_of_mod = 100.0f;
}

/**
 * Compressor, limiter
 */
__attribute__((noinline, optimize("O2"))) float compress(float val) {
    switch (*modulation) {
        // case MOD_LSB:
        case MOD_LSB_D: // lsb-d
        case MOD_USB_D: // usb-d
            return val * data->adc_input_coeff;
            break;
        case MOD_CW: // cw
        case MOD_CWR: // cwr
            return val;
            break;
        default:
            break;
    }

    // Remove DC offset
    val = dc_blocker(val, (1.0f - TX_DC_BLOCKER_ALPHA), &data->tx_dc_blocker);
    val *= data->adc_input_coeff;

    // Invert (make enabled by default)
    if (*cmp_enabled) {
        if (*modulation == MOD_AM) {
            val = soft_limiter(val);
        }
        return val;
    }

    // Put val and squared val to buffers
    float squared_val = val * val;
    data->comp.squared_sum += squared_val;

    ring_buf_put(&data->comp.dline, val);
    ring_buf_put(&data->comp.squared_acc, squared_val);

    if (!ring_buf_full(&data->comp.squared_acc)) {
        return 0.0f;
    }

    data->comp.squared_sum -= ring_buf_get(&data->comp.squared_acc);
    float old_val = ring_buf_get(&data->comp.dline);
    float rms;
    float squared_mean = data->comp.squared_sum / data->comp.squared_acc.size;
    if (squared_mean >= 0) {
        rms = sqrtf(squared_mean);
    } else {
        rms = 0.0f;
    }

    float rms_db = lin2db(rms);

    if (rms_db == NAN) {
        rms_db = UNITY_LVL;
    } else if (rms_db > 0.0f) {
        rms_db = 0.0f;
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

    if (*modulation == MOD_AM) {
        // Limit AM signal to prevent over-modulation
        old_val = soft_limiter(old_val);
    }

    return old_val;
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

// from CMSIS-DSP Include/dsp/filtering_functions.h
/**
  @brief Instance structure for single precision floating-point FIR decimator.
 */
typedef struct
{
    uint8_t M;            /**< decimation factor. */
    uint16_t numTaps;     /**< number of coefficients in the filter. */
    const float *pCoeffs; /**< points to the coefficient array. The array is of length numTaps.*/
    float *pState;        /**< points to the state variable array. The array is of length numTaps+blockSize-1. */
} arm_fir_decimate_instance_f32;


/**
 * fake fn (will be replaced with actual func address during linking)
 */
void arm_fill_f32 (float val, float* data, uint32_t size) {
    data[size-1] = val;
    return;
}


/**
 * Process AM/FM rx signal after demodulation
 */
float am_fm_rx_process(float val, float *i, float *q, uint8_t modulation) {
    // data_t *data = (data_t*)DATA_P;

    // Clear val array and fir decim state on change modulation
    arm_fir_decimate_instance_f32 *S = (arm_fir_decimate_instance_f32*)0x20008e74;
    float *val_acc = (float *)0x20008fa8;

    if (data->prev_modulation != modulation) {
        data->prev_modulation = modulation;
        val_acc[0] = 0.0f;
        val_acc[1] = 0.0f;
        arm_fill_f32(0.0f, S->pState, S->numTaps + 2 - 1);
    }

    switch (modulation)
    {
        case MOD_AM:
            // remove DC offset
            val = dc_blocker(val, (1.0f - RX_AM_DC_BLOCKER_ALPHA), &data->rx_am_dc_blocker);
            break;
        case MOD_NFM:
            val = dc_blocker(val, (1.0f - RX_FM_DC_BLOCKER_ALPHA), &data->rx_fm_dc_blocker);
            if (*sql) {
                data->fm_sql.iq_squared_sum += *i * *i + *q * *q;
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
                    val = 0.0f;
                }
            }
            break;
        default:
            break;
    }
    return val;
}


/**
 * Adaptive notch filter
 */

/**
 * @brief Instance structure for the floating-point Biquad cascade filter.
 */
typedef struct
{
    uint32_t numStages;   /**< number of 2nd order stages in the filter.  Overall order is 2*numStages. */

    // {x[n-1], x[n-2], y[n-1], y[n-2]}
    float *pState;        /**< Points to the array of state coefficients.  The array is of length 4*numStages. */

    // {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22, ...}
    float *pCoeffs; /**< Points to the array of coefficients.  The array is of length 5*numStages. */
} arm_biquad_casd_df1_inst_f32;


void anf_update() {
    arm_biquad_casd_df1_inst_f32 *flt = (arm_biquad_casd_df1_inst_f32 *)0x2000d994;
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
        flt->pCoeffs[1] = -an * gain;
        flt->pCoeffs[3] = -flt->pCoeffs[1];

        if (flt->pCoeffs[0] != gain) {
            flt->pCoeffs[0] = gain;
            flt->pCoeffs[2] = gain;
            flt->pCoeffs[4] = -r * gain;
        }
    }
}
