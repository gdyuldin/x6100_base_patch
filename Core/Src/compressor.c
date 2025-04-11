#include "compressor.h"

#include "math.h"
#include "stdint.h"
#include "log10f.c"
#include "powf.c"

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

#define UNITY_LVL -40.0f

#define DELAY 60
#define ATT_ALPHA 0.035957992f
#define RELEASE_ALPHA 0.00061015395f
#define RATIO_COMP 4.0f
#define RATIO_GATE 0.25f
#define TH_COMP (UNITY_LVL - 4.0f)
#define TH_GATE TH_COMP
// #define MAKEUP 24.0f
#define MAKEUP ((UNITY_LVL - TH_COMP) * (RATIO_COMP - 1.0f))


struct delay_line {
    float data[DELAY];
    uint8_t w;
    uint8_t r;
};

typedef struct {
    float l1_comp;
    float l1_gate;
    float peak;
    struct delay_line dline;
} comp_data_t;


static comp_data_t comp_d;
static comp_data_t *comp_d_p = &comp_d;

inline float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-6f);
}


inline void delay_line_put(struct delay_line *dline, float val) {
    dline->data[dline->w] = val;
    dline->w = (dline->w + 1) % DELAY;
}

inline float delay_line_get(struct delay_line *dline) {
    float val = dline->data[dline->r];
    dline->r = (dline->r + 1) % DELAY;
    return val;
}

inline uint8_t delay_line_full(struct delay_line *dline) {
    return (dline->w + 1) % DELAY == dline->r;
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

    delay_line_put(&comp_data->dline, val);

    float a_val = fabsf(val);
    a_val = lin2db(a_val);

    float gain_comp = a_val;
    float gain_gate = a_val;

    // Compute gains
    if (a_val > TH_COMP) {
        gain_comp = TH_COMP + (a_val - TH_COMP) / RATIO_COMP;
    } else if (a_val < TH_GATE) {
        gain_gate = TH_GATE + (a_val - TH_GATE) / RATIO_GATE;
        if (a_val - gain_gate > 40){
            gain_gate = a_val - 40;
        }
    }

    // level detector
    float comp_change = a_val - gain_comp;
    comp_data->l1_comp = MAX(comp_change, (1 - RELEASE_ALPHA) * comp_data->l1_comp + RELEASE_ALPHA * comp_change);
    float gate_change = a_val - gain_gate;
    comp_data->l1_gate = MIN(gate_change, (1 - RELEASE_ALPHA) * comp_data->l1_gate + RELEASE_ALPHA * gate_change);

    comp_data->peak = (1 - ATT_ALPHA) * comp_data->peak + ATT_ALPHA * (comp_data->l1_gate + comp_data->l1_comp);

    if (delay_line_full(&comp_data->dline)) {
        val = delay_line_get(&comp_data->dline);
        val *= db2lin(-comp_data->peak + MAKEUP);
    } else {
        val = 1e-12f;
    }

    return val;
}
