#include "if_shift.h"
#include "utils.h"
#include "external.h"

#include "math/sin_cos_tables.c"

#include <stdbool.h>
#include <stdint.h>

#define LUT_SIZE 1000

static CCMRAM struct {
    bool on;
    int32_t freq;
    int32_t step;
    int32_t i;
} state;

CCMRAM float sin_1000_table_ccm[ARRAY_SIZE(sin_1000_table)];

void if_shift_init(void) {
    state.step = 0;
    state.i = 0;
    state.on = false;
    state.freq = 0;
    ext_arm_copy_f32(sin_1000_table, sin_1000_table_ccm, ARRAY_SIZE(sin_1000_table));
}

/**
 * IF shift
 */

void if_shift_rx(void) {

    if (!state.on || (state.step == 0)) {
        return;
    }
    USE_OEM_SAMPLES_COUNT_VALUE_AS(samples_count);
    // Size 128 x 2
    float *iq = (float*) IQ_RF_FLOAT_IN;
    uint32_t counter = *samples_count;

    int32_t sin_k = state.i;

    // #pragma GCC unroll 4
    while (counter)
    {
        float i, q, i_a, q_a;
        i = iq[0];
        q = iq[1];
        // I is cos, Q is sin
        q_a = sin_1000_table_ccm[sin_k];
        i_a = sin_1000_table_ccm[sin_k + 250];
        float m1 = i * i_a;
        float m2 = q * q_a;
        float m3 = i * q_a;
        float m4 = q * i_a;
        i = m1 - m2;
        q = m3 + m4;
        iq[0] = i;
        iq[1] = q;

        sin_k += state.step;
        if (__builtin_expect(sin_k >= LUT_SIZE, 0)) {
            sin_k -= LUT_SIZE;
        } else if (__builtin_expect(sin_k < 0, 0)) {
            sin_k += LUT_SIZE;
        }

        iq += 2;
        counter--;
    }
    state.i = sin_k;
}

void if_shift_setup(bool on, int32_t freq)
{
    state.on = on;
    state.freq = freq;
    state.step = freq * LUT_SIZE / 100000;
}

int32_t if_shift_tx(int32_t lo_freq_shift) {
    if (state.on) {
        lo_freq_shift += state.freq;
    }
    return lo_freq_shift;
}
