#ifndef __UTILS_H
#define __UTILS_H

#include "math/pow.h"
#include "math/log10.h"

#include <stdint.h>

#define CCMRAM __attribute((section(".ccmram")))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#define CLIP(x, low, high) (x > high ? high : (x < low ? low : x))
#define ABS(x) (x < 0 ? -x: x)

typedef struct {
    float real;
    float imag;
} cfloat_t;

/**
 * DC blocker struct
 */
struct dc_blocker_t {
    float xm1;
    float ym1;
};


float soft_limiter(float val, float max_val);

float dc_blocker(float val, float k, struct dc_blocker_t *dc);


/**
 * Db <-> linear conversion
 */

inline float db2lin(float val) {
    return pow10f_c(val * 0.05f);
}

inline float lin2db(float val) {
    return 20.0f * log10f_c(val + 1e-16f);
}

void fill_zero(uint8_t *data, uint32_t size);

#endif //__UTILS_H




