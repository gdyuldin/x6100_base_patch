#ifndef __UTILS_H
#define __UTILS_H

#define _GNU_SOURCE
#include <math.h>
#include "powf.h"

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
    return powf10_c(val * 0.05f);
}

inline float lin2db(float val) {
    return 20.0f * log10f(val + 1e-16f);
}

#endif //__UTILS_H




