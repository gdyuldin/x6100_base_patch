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

#define UNITY_LVL -45.0f
#define NOISE_LVL (UNITY_LVL - 35.0f)

#define DELAY 60
#define ATT_ALPHA 0.035957992f
#define RELEASE_ALPHA 0.00061015395f
#define RATIO_COMP 4.0f
#define RATIO_GATE 0.25f
#define TH_COMP (NOISE_LVL + 22.0f)
#define TH_GATE TH_COMP
#define MAKEUP ((UNITY_LVL - TH_COMP) * (1.0f - 1.0f/RATIO_COMP))

#define DC_OFFSET_ALPHA (0.05f)

struct ring_buf {
    float data[DELAY];
    uint8_t w;
    uint8_t r;
};

typedef struct {
    float l1_comp;
    float l1_gate;
    float corr;
    struct ring_buf dline;
    struct ring_buf squared_acc;
    float squared_sum;
    float dc_ym1;
    float dc_xm1;
} comp_data_t;


static comp_data_t comp_d;
static comp_data_t *comp_d_p = &comp_d;

inline float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-6f);
}


void ring_buf_put(struct ring_buf *buf, float val) {
    buf->data[buf->w] = val;
    buf->w = (buf->w + 1) % DELAY;
}

float ring_buf_get(struct ring_buf *buf) {
    float val = buf->data[buf->r];
    buf->r = (buf->r + 1) % DELAY;
    return val;
}

uint8_t ring_buf_full(struct ring_buf *buf) {
    return (buf->w + 1) % DELAY == buf->r;
}

__attribute__((optimize("O1"))) void fill_zero(void) {
    uint32_t *area_end = (uint32_t*)0x20030000;
    uint32_t *area_start = area_end - sizeof(comp_data_t) / sizeof(uint32_t);

    for (;area_start < area_end; area_start++) {
        *area_start = 0;
    }
}


__attribute__((noinline)) float compress(float val) {
    uint8_t *modulation = (uint8_t *)0x2000a8cd;
    uint8_t *cmp_enabled = (uint8_t *)0x200000c4;
    uint8_t *cmp_level = (uint8_t *)0x200000c3;

    // Invert (make enabled by default)
    if (*cmp_enabled) {
        return val;
    }

    switch (*modulation) {
        // case 0: // lsb
        case 1: // lsb-d
        case 3: // usb-d
        case 4: // cw
        case 5: // cwr
            return val;
            break;
    }

    comp_data_t *comp_data = (comp_data_t*)comp_d_p;
    //sizeof(comp_data_t);


    float tmp = val - comp_data->dc_xm1 + (1.0f - DC_OFFSET_ALPHA) * comp_data->dc_ym1;
    comp_data->dc_xm1 = val;
    comp_data->dc_ym1 = tmp;
    val = tmp;

    ring_buf_put(&comp_data->dline, val);

    float squared_val = val * val;
    comp_data->squared_sum += squared_val;

    if (!ring_buf_full(&comp_data->squared_acc)) {
        ring_buf_put(&comp_data->squared_acc, squared_val);
        return 1e-12f;
    }

    comp_data->squared_sum -= ring_buf_get(&comp_data->squared_acc);
    ring_buf_put(&comp_data->squared_acc, squared_val);

    float rms = sqrtf_c(comp_data->squared_sum / DELAY);
    float rms_db = lin2db(rms);

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
    comp_data->l1_comp = MAX(comp_change, (1 - RELEASE_ALPHA) * comp_data->l1_comp + RELEASE_ALPHA * comp_change);
    float gate_change = rms_db - gain_gate;
    comp_data->l1_gate = MIN(gate_change, (1 - RELEASE_ALPHA) * comp_data->l1_gate + RELEASE_ALPHA * gate_change);

    comp_data->corr = (1 - ATT_ALPHA) * comp_data->corr + ATT_ALPHA * (comp_data->l1_gate + comp_data->l1_comp);

    val = ring_buf_get(&comp_data->dline);
    val *= db2lin(-comp_data->corr + MAKEUP);

    return val;
}
