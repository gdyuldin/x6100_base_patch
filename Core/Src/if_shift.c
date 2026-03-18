#include "if_shift.h"

#include "math/sin_cos.c"

#include <stdbool.h>
#include <stdint.h>


struct {
    bool on;
    int32_t freq;
    float step;
    float angle;
} state __attribute((section(".ccmram")));


void if_shift_init(void) {
    state.angle = 0.0f;
    state.step = 0.0f;
    state.on = false;
    state.freq = 0;
}

/**
 * IF shift
 */

void if_shift(void) {

    if (!state.on || (state.step == 0.0f)) {
        return;
    }
    USE_OEM_SAMPLES_COUNT_VALUE_AS(samples_count);
    // Size 128 x 2
    float *iq = (float*) IQ_RF_FLOAT_IN;
    float *stop = iq + 2 * *samples_count;
    float angle = state.angle;
    while (iq < stop)
    {
        float i, q, i_a, q_a;
        i = iq[0];
        q = iq[1];
        sin_cos(angle, &q_a, &i_a);
        float m1 = i * i_a;
        float m2 = q * q_a;
        float m3 = i * q_a;
        float m4 = q * i_a;
        i = m1 - m2;
        q = m3 + m4;
        iq[0] = i;
        iq[1] = q;

        iq += 2;

        angle += state.step;
        if (angle > M_TWOPI_F) {
            angle -= M_TWOPI_F;
        } else if (angle < 0) {
            angle += M_TWOPI_F;
        }
    }
    state.angle = angle;
}

void if_shift_setup(bool on, int32_t freq)
{
    state.on = on;
    state.freq = freq;
    state.step = freq * M_TWOPI_F / 100000.0f;;
}

int32_t tx_if_shift(int32_t lo_freq_shift) {
    if (state.on) {
        lo_freq_shift += state.freq;
    }
    return lo_freq_shift;
}
