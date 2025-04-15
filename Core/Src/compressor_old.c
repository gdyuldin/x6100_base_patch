#include "compressor.h"

#include "math.h"
#include "stdint.h"
#include "log10f.c"
#include "powf.c"

// static float rms_db;

// static float squared_sum;
// static unsigned int squared_counter;
// static char last_sign;

// static float scale;

// static float ratio = 2.0f;
// static float threshold = -40.0f;
// static float threshold2 = -60.0f;
// static unsigned int attack_ms = 10;
// static unsigned int release_ms = 200;


// #define SAMPLING_RATE 8000
// #define THRESHOLD -45
// #define RATIO_COMP 2
// #define RATIO_GATE 0.5f
// #define ATTACK_MS 100
// #define RELEASE_MS 1000

enum comp_state {
    STATE_IDLE,
    STATE_COMP,
    STATE_GATE,
};

inline float db2lin(float val) {
    return powf10_c(val / 20.0f);
}

inline float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-6f);
}

// inline float time_alpha(float t_ms) {
//     return 1.0f - expf(-logf(9.0f) / (SAMPLING_RATE * t_ms / 1000.0f));
// }

// __attribute__((noinline)) float compress(float val) {
//     static float act_scale;

//     // const float lin_th = db2lin(THRESHOLD);
//     // const float att_alpha = time_alpha(ATTACK_MS);
//     // const float release_alpha = time_alpha(RELEASE_MS);
//     // const float gate_add_scale = powf(lin_th, (1.0f / RATIO_COMP - 1.0f / RATIO_GATE));
//     const float lin_th = 0.0056234132f;
//     const float att_alpha = 0.0027427624f;
//     const float release_alpha = 0.00027461536f;
//     const float gate_add_scale = 2371.3738f;
//     const float ratio_comp = 2.0f;
//     const float ratio_gate = 0.5f;


//     float computed_scale;

//     float a_val = fabsf(val);
//     enum comp_state state;
//     if (a_val > lin_th) {
//         state = STATE_COMP;
//         computed_scale = powf(a_val, 1.0f / ratio_comp - 1.0f);
//     } else {
//         state = STATE_GATE;
//         computed_scale = powf(a_val, 1.0f / ratio_gate - 1.0f) * gate_add_scale;
//     }
//     act_scale += 1.0f;
//     unsigned int is_attack = ((state == STATE_COMP) && (computed_scale < act_scale)) ||
//                              ((state == STATE_GATE) && (computed_scale > act_scale));

//     if (is_attack) {
//         act_scale = computed_scale * att_alpha + act_scale * (1 - att_alpha);
//     } else {
//         act_scale = computed_scale * release_alpha + act_scale * (1 - release_alpha);
//     }

//     val *= act_scale;
//     act_scale -= 1.0f;
//     return val;
// }

// 1 - 7+ W
// 0.1 - 5W
// 0.05 - 5W
// 0.01 - 4.5W
// 0.009 - 4.3W
// 0.008 - 3.4W
// 0.007 - 2.7W
// 0.006 - 2.1W
// 0.003 - 0.6W

// 0.005 - 2.5
// 0.007 - 4.5
// 0.0075 - 5w
// 0.008 - 5 w

#define OFFSET (-30)
#define LEVEL (0.00725f)

static uint32_t step;
static uint32_t *step_p = &step;
// const float values[8] = {
//     1.0000000e-12,  7.8183150e-01,  9.7492790e-01,  4.3388382e-01,
//     -4.3388376e-01, -9.7492790e-01, -7.8183162e-01,  1.7484555e-07
// };
#define DELAY 60
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


__attribute__((optimize("O1"))) void fill_zero(void) {
    uint32_t *area_end = (uint32_t*)0x20030000;
    uint32_t *area_start = area_end - sizeof(comp_data_t) / sizeof(uint32_t);

    for (;area_start < area_end; area_start++) {
        *area_start = 0;
    }
}

__attribute__((noinline, optimize("O1"))) float compress(float val) {
    uint8_t *modulation = (uint8_t *)0x2000a8cd;
    uint8_t *cmp_enabled = (uint8_t *)0x200000c4;
    uint8_t *cmp_level = (uint8_t *)0x200000c3;

    // Invert (make enabled by default)
    if (*cmp_enabled) {
        return val;
    }

    switch (*modulation) {
        case 0: // lsb
        case 1: // lsb-d
        case 3: // usb-d
        case 4: // cw
        case 5: // cwr
            return val;
            break;
    }

    (*step_p)++;
    *step_p = *step_p & 7;

    // val = values[step];

    switch (*step_p)
    {
    case 0:
        val = 1.0000000e-12 * LEVEL;
        break;
    case 1:
        val = 7.8183150e-01 * LEVEL;
        break;
    case 2:
        val = 9.7492790e-01 * LEVEL;
        break;
    case 3:
        val = 4.3388382e-01 * LEVEL;
        break;
    case 4:
        val = -4.3388376e-01 * LEVEL;
        break;
    case 5:
        val = -9.7492790e-01 * LEVEL;
        break;
    case 6:
        val = -7.8183162e-01 * LEVEL;
        break;
    case 7:
        val = 1.7484555e-07 * LEVEL;
        break;

    default:
        break;
    }
    return val;
}
