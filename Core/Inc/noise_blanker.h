#ifndef __NOISE_BLANKER_H
#define __NOISE_BLANKER_H

#include "stdint.h"
#include "utils.h"
#include <stdbool.h>


void nb_init(void);

void nb_set_params(bool on, uint8_t width, uint8_t level);

cfloat_t nb_apply(cfloat_t iq);

// void nr_pause_update();

#endif // __NOISE_BLANKER_H
