#include "noise_blanker.h"
#include "external.h"
#include "ring_buf.h"

#include <arm_math.h>

#include "noise_blanker_data.c"

#define NB_SMA_SIZE 128

#define BUF_SIZE 128

#define NB_ENTER_SIZE ARRAY_SIZE(nb_enter_k)
#define NB_EXIT_SIZE ARRAY_SIZE(nb_exit_k)

#define NB_POW_ALPHA 0.002f
#define NB_MAX_PULSE_DURATION 250
#define NB_HOLD_OFF_DURATION 1000

typedef struct {
    cfloat_t data[BUF_SIZE];
    uint32_t w;
} iq_dline_t;


enum nb_state {
    STATE_WAIT,
    STATE_ENTER_1,
    STATE_ENTER_2,
    STATE_MUTE,
    STATE_EXIT,
};

__STATIC_FORCEINLINE void update_sma(float new_pow, float *pow_avg);
__STATIC_FORCEINLINE void iq_dline_reset(iq_dline_t *buf);
__STATIC_FORCEINLINE void iq_dline_put(iq_dline_t *buf, cfloat_t val);
__STATIC_FORCEINLINE cfloat_t iq_dline_get_delayed(iq_dline_t *buf, uint32_t delay);


CCMRAM struct {
    // simple moving average for power
    struct {
        float buf[NB_SMA_SIZE];
        uint32_t w;
        uint32_t r;
        float acc;
        float k;
    } sma_pow;

    // Delay line for IQ
    iq_dline_t iq_dline;

    enum nb_state state;
    float pwr_avg_last;

    // Thresholds
    float k_on;
    float k_off;

    uint16_t width_pre;
    uint16_t width_post;
    bool on;
    uint16_t counter;
} nb;


void nb_init(void) {
    // init sma
    ext_arm_fill_f32(0.0f, nb.sma_pow.buf, ARRAY_SIZE(nb.sma_pow.buf));
    nb.sma_pow.w = 0;
    nb.sma_pow.r = 0;
    nb.sma_pow.acc = 0.0f;
    nb.sma_pow.k = 1.0f / ARRAY_SIZE(nb.sma_pow.buf);

    // Init delay line for IQ
    ext_arm_fill_f32(0.0f, (float *)nb.iq_dline.data, ARRAY_SIZE(nb.iq_dline.data) * 2);
    iq_dline_reset(&nb.iq_dline);

    nb.counter = NB_HOLD_OFF_DURATION;
    nb.pwr_avg_last = 0.0f;
    nb.state = STATE_WAIT;

    nb_set_params(false, 50, 50);
}

void nb_set_params(bool on, uint8_t width, uint8_t level) {
    nb.on = on;
    // nb.width = width + 1;
    width += 1;
    nb.width_pre = MIN(width / 2 + 5, BUF_SIZE / 2 - 1);
    nb.width_post = width;

    // 3 - 18
    nb.k_on = 3.0f + 0.15f * level;
    nb.k_off = 1.0f + (nb.k_on - 1.0f) * 0.5f;
}

cfloat_t nb_apply(cfloat_t iq) {
    if (nb.on) {
        iq_dline_put(&nb.iq_dline, iq);

        float pwr = (iq.real * iq.real) + (iq.imag * iq.imag);

        float pwr_avg, diff, threshold;
        float scale = 1.0f;
        float k = nb.k_on;
        update_sma(pwr, &pwr_avg);

        switch (nb.state)
            {
            case STATE_WAIT:
                if (nb.counter) {
                    nb.counter--;
                    k *= 4;
                }
                threshold = pwr_avg * k;
                if (pwr > threshold) {
                    nb.state = STATE_ENTER_1;
                    nb.counter = 0;
                } else {
                    nb.pwr_avg_last = pwr_avg;
                }
                break;

            case STATE_ENTER_1:
                scale = nb_enter_k[nb.counter++];
                if (nb.counter == NB_ENTER_SIZE) {
                    nb.state = STATE_ENTER_2;
                    nb.counter = 0;
                }
                break;

            case STATE_ENTER_2:
                scale = 0.0f;
                nb.counter++;
                if (nb.counter == nb.width_pre) {
                    nb.state = STATE_MUTE;
                    nb.counter = 0;
                }
                break;

            case STATE_MUTE:
                scale = 0.0f;
                nb.counter++;
                if (nb.counter >= nb.width_post) {
                    if ((pwr < nb.pwr_avg_last * nb.k_off) ||
                        (nb.counter == NB_MAX_PULSE_DURATION)) {
                        nb.state = STATE_EXIT;
                        nb.counter = 0;
                    }
                }
                break;

            case STATE_EXIT:
                scale = nb_exit_k[nb.counter++];
                if (nb.counter == NB_EXIT_SIZE) {
                    nb.state = STATE_WAIT;
                    nb.counter = NB_HOLD_OFF_DURATION;
                }
                break;

            default:
                break;
        }

        // Apply
        cfloat_t new_val = iq_dline_get_delayed(&nb.iq_dline, nb.width_pre + NB_ENTER_SIZE);
        if (scale == 0.0f) {
            new_val = (cfloat_t){0.0f, 0.0f};
        } else if (scale != 1.0f) {
            new_val.real *= scale;
            new_val.imag *= scale;
        }
        return new_val;
    } else {
        iq_dline_reset(&nb.iq_dline);
        return iq;
    }
}

__STATIC_FORCEINLINE void update_sma(float new_pwr, float *pwr_avg) {
    nb.sma_pow.acc += new_pwr;
    nb.sma_pow.buf[nb.sma_pow.w] = new_pwr;
    nb.sma_pow.w = (nb.sma_pow.w + 1) & (NB_SMA_SIZE - 1);
    float pwr_sum = nb.sma_pow.acc;
    if (__builtin_expect(nb.sma_pow.w == nb.sma_pow.r, 1)) {
        nb.sma_pow.acc -= nb.sma_pow.buf[nb.sma_pow.r];
        nb.sma_pow.r = (nb.sma_pow.r + 1) & (NB_SMA_SIZE - 1);
    }
    *pwr_avg = pwr_sum * nb.sma_pow.k;
}


__STATIC_FORCEINLINE void iq_dline_reset(iq_dline_t *buf) {
    buf->w = 0;
}

__STATIC_FORCEINLINE void iq_dline_put(iq_dline_t *buf, cfloat_t val) {
    buf->data[buf->w] = val;
    buf->w = (buf->w + 1) & (BUF_SIZE - 1);
}

__STATIC_FORCEINLINE cfloat_t iq_dline_get_delayed(iq_dline_t *buf, uint32_t delay) {
    uint32_t r = (buf->w - delay) & (BUF_SIZE - 1);
    return buf->data[r];
}

