#include "compressor.h"

#include <dsp/controller_functions.h>

#include "offsets.h"

#include "noise_reduction.h"
#include "noise_blanker.h"

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
#include "vox.h"
#include "cw_peak.h"

// #include <dsp/fast_math_functions.h>
// #include <dsp/support_functions.h>


// #define MAX(a, b) (a > b ? a : b)
// #define MIN(a, b) (a < b ? a : b)

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

#define RX_AM_DC_BLOCKER_ALPHA (0.03f)
#define RX_FM_DC_BLOCKER_ALPHA (0.01f)




enum __attribute__((__packed__)) rx_tx_process_state {
    RX_TX_STATE_NORMAL,
    RX_TX_STATE_PIN_DISCHARGE,
    RX_TX_STATE_MUTE,
    RX_TX_STATE_WAIT_PIN_SWITCH,
};

enum iq_offset_remove_state {
    STATE_NORMAL,
    STATE_WAITING_PEAK,
    STATE_MUTE_PEAK,
    STATE_MUTE_RECOVER,
    STATE_RECOVER,
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

    struct {
        int32_t i_prev;
        int32_t q_prev;
        int32_t i_mean;
        int32_t q_mean;

        uint32_t counter;
        uint32_t scale_i;
        uint8_t discharge_k;

        int8_t i_sign;
        int8_t q_sign;
        bool i_mute;
        bool q_mute;
        enum iq_offset_remove_state state;
    } rx_iq;

    /* General data */
    struct {
        enum mod_t mode;
        uint32_t att;
        uint32_t pre;
        uint8_t tx;
        uint8_t hw_filter_id;
    } prev_state;
    bool swr_scan;
    uint8_t band_id;

    /* Debug and pad */
    // tone

} data_t;
// } __attribute__ ((aligned (16))) data_t;

static CCMRAM data_t data_arr;

static data_t *data = &data_arr;



/**
 * Actual values pointers
 */

// Compressor values
static uint8_t *cmp_enabled = (uint8_t *)CMP_ENABLED_VALUE;

static float *am_carrier_lvl = (float *)AM_CARRIER_LEVEL_VALUE;

#define ADDR_IQ_INT_IN (0x20005918)

#define PEAK_TH 10000

static float lin_arr[] = {
1.        , 0.947061  , 0.89692454, 0.84944225, 0.80447363,
       0.7618856 , 0.72155214, 0.68335389, 0.64717782, 0.61291687,
       0.58046966, 0.54974018, 0.52063748, 0.49307546, 0.46697254,
       0.44225148, 0.41883913, 0.3966662 , 0.37566709, 0.35577965,
       0.33694503, 0.3191075 , 0.30221427, 0.28621534, 0.27106339,
       0.25671357, 0.24312341, 0.2302527 , 0.21806335, 0.20651929,
       0.19558637, 0.18523222, 0.17542621, 0.16613932, 0.15734408,
       0.14901444, 0.14112576, 0.13365471, 0.12657916, 0.11987818,
       0.11353195, 0.10752169, 0.10182959, 0.09643884, 0.09133346,
       0.08649836, 0.08191922, 0.0775825 , 0.07347536, 0.06958565,
       0.06590185, 0.06241308, 0.05910899, 0.05597982, 0.0530163 ,
       0.05020967, 0.04755162, 0.04503429, 0.04265022, 0.04039236,
       0.03825403, 0.0362289 , 0.03431098, 0.03249459, 0.03077436,
       0.02914519, 0.02760228, 0.02614104, 0.02475716, 0.02344654,
       0.0222053 , 0.02102978, 0.01991648, 0.01886212, 0.01786358,
       0.0169179 , 0.01602228, 0.01517408, 0.01437078, 0.01361   ,
       0.0128895 , 0.01220715, 0.01156091, 0.01094889, 0.01036927,
       0.00982033, 0.00930045, 0.00880809, 0.0083418 , 0.00790019,
       0.00748197, 0.00708588, 0.00671076, 0.0063555 , 0.00601904,
       0.0057004 , 0.00539863, 0.00511283, 0.00484216, 0.00458582,
       0.00434305, 0.00411314, 0.00389539, 0.00368917, 0.00349387,
       0.00330891, 0.00313374, 0.00296784, 0.00281073, 0.00266193,
       0.00252101, 0.00238755, 0.00226116, 0.00214145, 0.00202809,
       0.00192072, 0.00181904, 0.00172274, 0.00163154, 0.00154517,
       0.00146337, 0.0013859 , 0.00131253, 0.00124305, 0.00117724,
       0.00111492, 0.0010559 , 0.001
};


static inline void switch_to_normal(void) {
    data->rx_iq.state = STATE_NORMAL;
}

static inline void switch_to_waiting_peak(void) {
    data->rx_iq.state = STATE_WAITING_PEAK;
    data->rx_iq.scale_i = 0;

    data->rx_iq.i_mute = false;
    data->rx_iq.q_mute = false;
    data->rx_iq.i_sign = 0;
    data->rx_iq.q_sign = 0;
}

static inline void switch_to_mute_peak(void) {
    data->rx_iq.state = STATE_MUTE_PEAK;
    data->rx_iq.counter = 1024 * 2;
}

static inline void switch_to_mute_recover(void) {
    data->rx_iq.state = STATE_MUTE_RECOVER;
    data->rx_iq.counter = 128;
}

static inline void switch_to_recover(void) {
    data->rx_iq.state = STATE_RECOVER;
    data->rx_iq.counter = 4096;
    data->rx_iq.scale_i = ARRAY_SIZE(lin_arr) - 1;
    data->rx_iq.scale_i = MIN(data->rx_iq.scale_i, data->rx_iq.counter);
}

static inline void start_peak_filter(bool tx) {
    if (tx) {
        switch_to_mute_peak();
        data->rx_iq.counter = 128 * 100;
        data->rx_iq.discharge_k = 14;
    } else {
        data->rx_iq.counter = 8192;
        data->rx_iq.discharge_k = 5;
        switch_to_waiting_peak();
        // switch_to_mute_peak();
        // data->rx_iq.counter = 1024*2;
    }
}

void remove_iq_offset(int32_t *iq) {
    // uint32_t n = iq - (int32_t*)(ADDR_IQ_INT_IN);
    USE_OEM_TX_FLAG_AS(pTx);

    int32_t *i = iq;
    int32_t *q = iq + 1;

    if (*pTx) {
        *i = 0;
        *q = 0;
        return;
    }

    if (data->rx_iq.counter) {
        data->rx_iq.counter--;
    }

    if ((data->rx_iq.state == STATE_RECOVER) || (data->rx_iq.state == STATE_MUTE_RECOVER)) {
        // Discharge
        data->rx_iq.i_mean -= data->rx_iq.i_mean >> data->rx_iq.discharge_k;
        data->rx_iq.q_mean -= data->rx_iq.q_mean >> data->rx_iq.discharge_k;
    }

    int32_t i_shifted = *i - data->rx_iq.i_mean;
    int32_t q_shifted = *q - data->rx_iq.q_mean;

    data->rx_iq.i_mean += i_shifted >> 9;
    data->rx_iq.q_mean += q_shifted >> 9;

    if (data->rx_iq.state == STATE_MUTE_PEAK) {
        // if (data->rx_iq.scale_i < ARRAY_SIZE(lin_arr)){
        //     i_shifted = lin_arr[data->rx_iq.scale_i] * data->rx_iq.i_prev;
        //     q_shifted = lin_arr[data->rx_iq.scale_i] * data->rx_iq.q_prev;
        //     data->rx_iq.scale_i++;
        // } else {
        //     i_shifted = 0;
        //     q_shifted = 0;
        // }
        i_shifted = 0;
        q_shifted = 0;
        if (!data->rx_iq.counter) {
            data->rx_iq.i_mean = *i;
            data->rx_iq.q_mean = *q;
            switch_to_mute_recover();
        }
    }

    if (data->rx_iq.state == STATE_WAITING_PEAK) {
        // Find peak
        int64_t di = (int64_t)i_shifted - data->rx_iq.i_prev;
        int64_t dq = (int64_t)q_shifted - data->rx_iq.q_prev;

        if ((di > PEAK_TH) || (dq > PEAK_TH) || (di < -PEAK_TH) || (dq < -PEAK_TH)) {
            switch_to_mute_peak();
            i_shifted = data->rx_iq.i_prev / 2;
            q_shifted = data->rx_iq.q_prev / 2;
        } else {
            data->rx_iq.i_prev = i_shifted;
            data->rx_iq.q_prev = q_shifted;
        }

        // if (data->rx_iq.i_mute) {
        //     i_shifted = 0;
        // }  else {
        //     int8_t new_sign = i_shifted > 0 ? 1 : -1;
        //     if (data->rx_iq.i_sign == 0) {
        //         data->rx_iq.i_sign = new_sign;
        //     } else if (new_sign != data->rx_iq.i_sign) {
        //         data->rx_iq.i_mute = true;
        //         i_shifted = 0;
        //     }
        // }

        // if (data->rx_iq.q_mute) {
        //     q_shifted = 0;
        // }  else {
        //     int8_t new_sign = q_shifted > 0 ? 1 : -1;
        //     if (data->rx_iq.q_sign == 0) {
        //         data->rx_iq.q_sign = new_sign;
        //     } else if (new_sign != data->rx_iq.q_sign) {
        //         data->rx_iq.q_mute = true;
        //         q_shifted = 0;
        //     }
        // }
        // if (data->rx_iq.scale_i < ARRAY_SIZE(lin_arr)){
        //     i_shifted = lin_arr[data->rx_iq.scale_i] * data->rx_iq.i_prev;
        //     q_shifted = lin_arr[data->rx_iq.scale_i] * data->rx_iq.q_prev;
        //     data->rx_iq.scale_i++;
        // } else {
        //     i_shifted = 0;
        //     q_shifted = 0;
        // }

        if (!data->rx_iq.counter) {
            switch_to_normal();
        }
    }

    if (data->rx_iq.state == STATE_MUTE_RECOVER)  {
        if (!data->rx_iq.counter) {
            switch_to_recover();
        } else {
            i_shifted = 0;
            q_shifted = 0;
        }
    }

    if (data->rx_iq.state == STATE_RECOVER) {
        if (data->rx_iq.scale_i > 0) {
            i_shifted = lin_arr[data->rx_iq.scale_i] * i_shifted;
            q_shifted = lin_arr[data->rx_iq.scale_i] * q_shifted;
            data->rx_iq.scale_i--;
        }
        if (!data->rx_iq.counter) {
            switch_to_normal();
        }
    }


    *i = i_shifted;
    *q = q_shifted;
}


inline static void fast_iq_offset_counter_setup() {
    USE_OEM_TX_FLAG_AS(pTx);

    int32_t *input_data = *(int32_t **)INPUT_RF_SIGNAL_INT_ADDR;
    USE_OEM_SAMPLES_COUNT_VALUE_AS(samples_count);

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
            for (size_t i = 0; i < *samples_count * 2; i+=2) {
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
    fill_zero((uint8_t*)data, sizeof(*data));

    ring_buf_init(&data->comp.dline, data->comp.dline_data, ARRAY_SIZE(data->comp.dline_data));
    ring_buf_init(&data->comp.squared_acc, data->comp.squared_acc_data, ARRAY_SIZE(data->comp.squared_acc_data));

    set_comp_ratio(2.0f);
    set_comp_threshold_offset(0.0f);
    set_comp_makeup_offset(0.0f);

    set_pwr(5.0f);

    set_flow_params(x6100_flow_fp32);

    anf_init();
    comm_init();
    if_shift_init();
    fm_demod_init();
    vox_init();
    cw_peak_init();

    // Init frequencies for NR correct initialization
    struct filter_freqs_t *filter_frequencies = (struct filter_freqs_t *)FILTER_FREQUENCIES;

    filter_frequencies[0].low = 300;
    filter_frequencies[1].low = 300;
    filter_frequencies[0].high = 3000;
    filter_frequencies[1].high = 3000;

    // Noise reduction init
    nr_init();
    nb_init();
}


/**
 * Update signal processing parameters. Inject, calls at beginning of the DMA process.
 */

enum demod_info {
    DEMOD_SSB,
    DEMOD_AM,
    DEMOD_FM,
};

static inline enum demod_info get_demod_info(uint8_t mode) {
    enum demod_info info = DEMOD_SSB;
    if (mode == MOD_AM) {
        info = DEMOD_AM;
    } else if (mode == MOD_NFM) {
        info = DEMOD_FM;
    }
    return info;
}

static void reset_filters_states_on_changes() {
    USE_OEM_MODULATION_AS(pMode);

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

    bool clear_after_demod = false;
    bool clear_before_demod = false;

    bool *reset = (bool*)RESET_FILTERS_STATE;
    if (*reset) {
        clear_after_demod = true;
        start_peak_filter(false);

        // Silence after reset
        // *(uint32_t*)(0x2000a704) = 16;
        // clear_before_demod = true;
        // Not clear all filters
        // *reset = false;
        // return;
    }


    uint32_t *decim2_i = (uint32_t*)FIR_DECIM_2_I;

    if (*pMode != data->prev_state.mode) {
        if (get_demod_info(*pMode) != get_demod_info(data->prev_state.mode)) {
            *reset = true;
            // clear_before_demod = true;
        }
        data->prev_state.mode = *pMode;
    }
    if (*pre != data->prev_state.pre) {
        data->prev_state.pre = *pre;
        start_peak_filter(false);
        clear_after_demod = true;
    }
    if (*att != data->prev_state.att) {
        data->prev_state.att = *att;
        start_peak_filter(false);
        clear_after_demod = true;
    }

    uint8_t *hw_filter_id = (uint8_t *)(0x200000f7);
    if (*hw_filter_id != data->prev_state.hw_filter_id) {
        *reset = true;
        data->prev_state.hw_filter_id = *hw_filter_id;
    }

    if (clear_after_demod) {
        // interp
        ext_arm_fill_f32(0.0f, (float*)FIR_INTERP_8_STATE, 8);
        ext_arm_fill_f32(0.0f, (float*)FIR_INTERP_8_BUF, 8);

        // decim_2
        ext_arm_fill_f32(0.0f, (float*)FIR_DECIM_2_BUF, 2);
        ext_arm_fill_f32(0.0f, (float*)FIR_DECIM_2_STATE, 0x25);

        *decim2_i = 0;
    }

    if (clear_before_demod) {
#define addr_fir_decim_8_0_pstate (0x20008af0)
#define addr_fir_decim_8_1_pstate (0x20008d3c)

#define addr_fir_decim_8_0_buf    (0x20008c10)
#define addr_fir_decim_8_1_buf    (0x20008e5c)

#define addr_fir_decim_8_0_i    (0x20008c0c)
#define addr_fir_decim_8_1_i    (0x20008e58)

#define addr_fir_decim_4_0_pstate (0x200087e8)
#define addr_fir_decim_4_1_pstate (0x20008934)

#define addr_fir_decim_4_0_buf    (0x20008888)
#define addr_fir_decim_4_1_buf    (0x200089d4)

#define addr_fir_decim_4_0_i    (0x20008884)
#define addr_fir_decim_4_1_i    (0x200089d0)

#define addr_i_lpf_state        (0x20009178)
#define addr_q_lpf_state        (0x20009244)


        ext_arm_fill_f32(0.0f, (float*)addr_i_lpf_state, 0x14);
        ext_arm_fill_f32(0.0f, (float*)addr_q_lpf_state, 0x14);


        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_8_0_pstate, 0x47);
        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_8_1_pstate, 0x47);

        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_8_0_buf, 8);
        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_8_1_buf, 8);

        *(uint32_t*)(addr_fir_decim_8_0_i) = 0;
        *(uint32_t*)(addr_fir_decim_8_1_i) = 0;


        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_4_0_pstate, 0x27);
        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_4_1_pstate, 0x27);

        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_4_0_buf, 4);
        ext_arm_fill_f32(0.0f, (float*)addr_fir_decim_4_1_buf, 4);

        *(uint32_t*)(addr_fir_decim_4_0_i) = 0;
        *(uint32_t*)(addr_fir_decim_4_1_i) = 0;


        // arm_fill_f32(0.0,data.f.a_filters_1[1].pState,0x14);             0x200093dc
        // arm_fill_f32(0.0,data.f.a_filters_1[0].pState,0x14);             0x20009310
        // ext_arm_fill_f32(0.0f, (float*)(0x200093dc), 0x14);
        // ext_arm_fill_f32(0.0f, (float*)(0x20009310), 0x14);

    }

    if (*reset) {
        nr_pause_update();
    }
}

void configure(void) {
    USE_OEM_TX_FLAG_AS(pTx);
    USE_OEM_I2C_REGS_AS(i2c_regs);
    // USE_OEM_TX_STATE_FLAGS_AS(txState);

    // Stop VOX on any other TX flag
    // if (*txState & (~TX_STATE_VOX)) {
    //     vox_stop();
    // }

    reset_filters_states_on_changes();
    nr_prepare();

    // Reset coefficients on SWR scan
    bool swr_scan = i2c_regs[x6100_sple_atue_trx] & x6100_swrscan_trx;
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
    // fast_iq_offset_counter_setup();

    // Reset ring buffers (for compressor) on RX/TX state change
    if (data->prev_state.tx != *pTx) {
        data->prev_state.tx = *pTx;
        if (*pTx) {
            ring_buf_reset(&data->comp.dline);
            nr_pause_update();
        } else {
            start_peak_filter(true);
        }
    }

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
    vox_compute();
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
            k = ext_sqrt_f32(pow_scale);
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
        case MOD_CW:
        case MOD_CWR:
            return;
            break;
        default:
            break;
    }
    float val = *pval * data->tx_amp_coeffs.adc_input_coeff;

    val = vox_process_audio_sample(val);

    switch (*pMode) {
        case MOD_LSB_D:
        case MOD_USB_D:
            *pval = val;
            return;
            break;
        default:
            break;
    }

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
    float squared_mean = data->comp.squared_sum / data->comp.squared_acc.size;
    float rms_db;
    if (squared_mean >= 0) {
        rms_db = pow2db(squared_mean);
    } else {
        rms_db = -120.0f;
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

    if (isnan(rms_db)) {
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
