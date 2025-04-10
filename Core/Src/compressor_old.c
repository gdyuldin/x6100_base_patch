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

#define OFFSET (-30)
__attribute__((noinline)) float compress(float val) {
    uint8_t *modulation = (uint8_t *)0x2000853d;
    uint8_t *cmp_enabled = (uint8_t *)0x2000005c;
    uint8_t *cmp_level = (uint8_t *)0x2000005b;

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
    static float act_scale;

    // const float lin_th = db2lin(THRESHOLD);
    // const float att_alpha = time_alpha(ATTACK_MS);
    // const float release_alpha = time_alpha(RELEASE_MS);
    // const float gate_add_scale = powf(lin_th, (1.0f / RATIO_COMP - 1.0f / RATIO_GATE));
    const float att_alpha = 0.0036553438f;
    const float release_alpha = 0.00036613704f;
    const float gate_add_scale = 175.5f;
    const float ratio_comp = 4.0f;
    const float ratio_gate = 0.1f;
    const float threshold = -18.0f;

    float computed_scale;

    float a_val = fabsf(val);
    a_val = lin2db(a_val) - OFFSET;
    enum comp_state state;
    if (a_val > threshold) {
        state = STATE_COMP;
        computed_scale = a_val * (1.0f / ratio_comp - 1.0f);
    } else {
        state = STATE_GATE;
        computed_scale = a_val * (1.0f / ratio_gate - 1.0f) + gate_add_scale;
    }

    unsigned int is_attack = ((state == STATE_COMP) && (computed_scale < act_scale)) ||
                                ((state == STATE_GATE) && (computed_scale > act_scale));

    if (is_attack) {
        act_scale = computed_scale * att_alpha + act_scale * (1 - att_alpha);
    } else {
        act_scale = computed_scale * release_alpha + act_scale * (1 - release_alpha);
    }

    val *= db2lin(act_scale - 2.6f);

    return val;
}
