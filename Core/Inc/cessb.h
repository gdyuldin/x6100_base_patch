#ifndef __CESSB_H
#define __CESSB_H

#include "stdint.h"
#include "stdbool.h"

void cessb_init(void);
void cessb_update_filter(uint16_t low, uint16_t high);
void cessb_set_params(bool on, float power_up_db);
void cessb_process(float *i, float *q);

#endif //__CESSB_H
