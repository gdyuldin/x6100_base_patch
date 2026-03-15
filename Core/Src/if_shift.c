#include "if_shift.h"

#include "sin_cos.c"

#include <stdbool.h>
#include <stdint.h>


struct {
    bool on;
    int32_t freq;
    float step;
    float angle;
} data;


void if_shift_init(void) {
    data.angle = 0.0f;
    data.step = 0.0f;
    data.on = false;
    data.freq = 0;
}

/**
 * IF shift
 */

void if_shift(void) {
    if (!data.on || (data.step == 0.0f)) {
        return;
    }
    // Size 128 x 2
    float *iq = (float*) IQ_RF_FLOAT_IN;
    float *stop = iq + 256;
    float angle = data.angle;
    do
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

        angle += data.step;
        if (angle > M_TWOPI_F) {
            angle -= M_TWOPI_F;
        } else if (angle < 0) {
            angle += M_TWOPI_F;
        }
    } while (iq != stop);
    data.angle = angle;
}

void if_shift_setup(bool on, int32_t freq)
{
    data.on = on;
    data.freq = freq;
    data.step = freq * M_TWOPI_F / 100000.0f;;
}

int32_t tx_if_shift(int32_t lo_freq_shift) {
    if (data.on) {
        lo_freq_shift += data.freq;
    }
    return lo_freq_shift;
}
