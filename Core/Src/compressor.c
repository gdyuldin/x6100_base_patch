#include "compressor.h"

#include "math.h"
#include "stdint.h"
#include "log10f.c"
#include "powf.c"
#include "sqrtf.c"

// 1 - 7+ W
// 0.1 - 5W
// 0.05 - 5W
// 0.01 - 4.5W
// 0.009 - 4.3W
// 0.008 - 3.4W
// 0.007 - 2.7W
// 0.006 - 2.1W
// 0.003 - 0.6W

#define MAX(a, b) (a > b ? a : b)
#define MIN(a, b) (a < b ? a : b)

/*
Compressor values
*/

#define UNITY_LVL -42.0f
// #define UNITY_LVL -10.0f
#define NOISE_LVL (UNITY_LVL - 35.0f)

#define DELAY 60
#define ATT_ALPHA 0.035957992f
#define RELEASE_ALPHA 0.00061015395f
#define RATIO_COMP 4.0f
#define RATIO_GATE 0.25f
#define TH_COMP (NOISE_LVL + 25.0f)
#define TH_GATE TH_COMP
#define MAKEUP ((UNITY_LVL - TH_COMP) * (1.0f - 1.0f/RATIO_COMP) - 2.0f)

#define DC_OFFSET_ALPHA (0.05f)

/*
Limiter
*/

#define LIMITER_MAX_VAL 0.00769f


struct ring_buf {
    float data[DELAY];
    uint32_t w;
    uint32_t r;
};

typedef struct {
    float l1_comp;
    float l1_gate;
    float corr;
    struct ring_buf dline;
    struct ring_buf squared_acc;
    float squared_sum;

    // dc block
    float dc_xm1;
    float dc_ym1;

    // tx amp coeffs
    float tx_p_k_ssb;
    float tx_p_k_cw;
    float tx_p_k_am;
    float tx_p_k_fm;

    // tone
    uint32_t tone_step;
    uint32_t _pad;
} data_t;

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

#define DATA_P (0x20030000 - sizeof(data_t))
#define MODULATION_P 0x2000a8cd
#define CMP_ENABLED_P 0x200000c4
#define CMP_LEVEL_P 0x200000c3

static data_t data;
static data_t *data_p = &data;

/*
Helper functions
*/

inline float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-6f);
}

/*
Operate with buffer
*/

void ring_buf_put(struct ring_buf *buf, float val) {
    buf->data[buf->w] = val;
    if (buf->w == DELAY - 1) {
        buf->w = 0;
    } else {
        buf->w++;
    }
}

float ring_buf_get(struct ring_buf *buf) {
    float val = buf->data[buf->r];
    if (buf->r == DELAY - 1) {
        buf->r = 0;
    } else {
        buf->r++;
    }
    return val;
}

uint8_t ring_buf_full(struct ring_buf *buf) {
    return buf->w == buf->r;
}

/*
Initialize data with zeros (at start)
*/

__attribute__((optimize("O1"))) void fill_zero(void) {
    uint32_t *area_end = (uint32_t*)0x20030000;
    uint32_t *area_start = area_end - sizeof(data_t) / sizeof(uint32_t);

    for (;area_start < area_end; area_start++) {
        *area_start = 0;
    }
}

/*
Soft limiter
*/

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

// #define TONE_TEST

#ifdef TONE_TEST
#define LEVEL (0.0087f)

/*
Tone generator for levels testing
*/
__attribute__((noinline, optimize("O1"))) float compress(float val) {
    uint8_t *modulation = (uint8_t *)0x2000a8cd;

    switch (*modulation) {
        case MOD_LSB_D:
        case MOD_USB_D:
        case MOD_CW:
        case MOD_CWR:
            return val;
            break;
    }

    data_t *data = (data_t*)DATA_P;

    data->tone_step++;
    data->tone_step = data->tone_step & 7;

    switch (data->tone_step)
    {
    case 0:
        val = 0.0f * LEVEL;
        break;
    case 1:
        val = 7.07106781e-01f * LEVEL;
        break;
    case 2:
        val = 1.0f * LEVEL;
        break;
    case 3:
        val = 7.07106781e-01f * LEVEL;
        break;
    case 4:
        val = 1.22464680e-16f * LEVEL;
        break;
    case 5:
        val = -7.07106781e-01f * LEVEL;
        break;
    case 6:
        val = -1.0f * LEVEL;
        break;
    case 7:
        val = -7.07106781e-01f * LEVEL;
        break;

    default:
        break;
    }
    return val;
}

#else

__attribute__((noinline, optimize("O2"))) float compress(float val) {
    enum mod_t *modulation = (enum mod_t *)MODULATION_P;
    uint8_t *cmp_enabled = (uint8_t *)CMP_ENABLED_P;
    uint8_t *cmp_level = (uint8_t *)CMP_LEVEL_P;
    switch (*modulation) {
        // case MOD_LSB:
        case MOD_LSB_D: // lsb-d
        case MOD_USB_D: // usb-d
        case MOD_CW: // cw
        case MOD_CWR: // cwr
            return val;
            break;
        default:
            break;
    }

    // Invert (make enabled by default)
    if (*cmp_enabled) {
        if (*modulation == MOD_AM) {
            val = soft_limiter(val);
        }
        return val;
    }

    // data_t *comp_data = (data_t*)data_p;
    data_t *data = (data_t*)DATA_P;

    // Remove DC offset
    float tmp = val - data->dc_xm1 + (1.0f - DC_OFFSET_ALPHA) * data->dc_ym1;
    data->dc_xm1 = val;
    data->dc_ym1 = tmp;
    val = tmp;

    // Put val and squared val to buffers
    float squared_val = val * val;
    data->squared_sum += squared_val;

    ring_buf_put(&data->dline, val);
    ring_buf_put(&data->squared_acc, squared_val);

    if (!ring_buf_full(&data->squared_acc)) {
        return 0.0f;
    }

    data->squared_sum -= ring_buf_get(&data->squared_acc);
    float old_val = ring_buf_get(&data->dline);

    float rms = sqrtf_c(data->squared_sum / DELAY);

    float rms_db = lin2db(rms);

    if (rms_db == NAN) {
        rms_db = UNITY_LVL;
    } else if (rms_db > 0.0f) {
        rms_db = 0.0f;
    }

    float gain_comp = rms_db;
    float gain_gate = rms_db;

    // Compute gains
    if (rms_db > TH_COMP) {
        gain_comp = TH_COMP + (rms_db - TH_COMP) / RATIO_COMP;
    } else if (rms_db < TH_GATE) {
        gain_gate = TH_GATE + (rms_db - TH_GATE) / RATIO_GATE;
        if (rms_db - gain_gate > 40){
            gain_gate = rms_db - 40;
        }
    }

    // level detector
    float comp_change = rms_db - gain_comp;
    data->l1_comp = MAX(comp_change, (1 - RELEASE_ALPHA) * data->l1_comp + RELEASE_ALPHA * comp_change);
    float gate_change = rms_db - gain_gate;
    data->l1_gate = MIN(gate_change, (1 - RELEASE_ALPHA) * data->l1_gate + RELEASE_ALPHA * gate_change);

    data->corr = (1 - ATT_ALPHA) * data->corr + ATT_ALPHA * (data->l1_gate + data->l1_comp);

    float corr = -data->corr + MAKEUP;

    old_val *= db2lin(corr);

    if (*modulation == MOD_AM) {
        // Limit AM signal to prevent over-modulation
        old_val = soft_limiter(old_val);
    }

    return old_val;
}

#endif


/*
Amplify TX IQ signal for TX output power
*/
__attribute__((noinline)) void tx_amp(float *i, float *q) {
    data_t *data = (data_t*)DATA_P;
    enum mod_t *modulation = (enum mod_t *)MODULATION_P;
    float k;
    switch (*modulation)
    {
        case MOD_LSB:
        case MOD_LSB_D:
        case MOD_USB:
        case MOD_USB_D:
            k = data->tx_p_k_ssb;
            break;
        case MOD_CW:
        case MOD_CWR:
            k = data->tx_p_k_cw;
            break;
        case MOD_AM:
            return;
            break;
        case MOD_NFM:
            k = data->tx_p_k_fm;
            break;
        default:
            k = 1.0f;
            break;
    }
    *i *= k;
    *q *= k;
}


/*
Set IQ scale on changing TX power
*/
__attribute__((noinline)) void tx_coeff_calc(float pwr) {
    data_t *data = (data_t*)DATA_P;
    float *am_carrier_lvl = (float *)0x2000a174;
    float *am_depth_of_mod = (float *)0x2000a178;
    float *fm_depth_of_mod = (float *)0x2000a184;
    float k = sqrtf_c(pwr / 10.0f);
    // set coeffs
    data->tx_p_k_ssb = k;
    data->tx_p_k_cw = k;
    data->tx_p_k_fm = 7.6e-2f * k;

    // for 2.5 w carrier at 10w output
    // *am_carrier_lvl = 0.025f * k;
    // *am_depth_of_mod = 3.25f * k;

    // for 5W carrier at 10W output
    *am_carrier_lvl = 0.0353f * k;
    *am_depth_of_mod = 4.6f * k;

    // Set FM depth of mod
    *fm_depth_of_mod = 100.0f;
}
