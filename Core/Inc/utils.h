#ifndef __UTILS_H
#define __UTILS_H

// #include <math.h>
#include "math/pow.h"
#include "math/log10.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

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

#endif //__UTILS_H




