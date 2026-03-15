#ifndef __IF_SHIFT_H
#define __IF_SHIFT_H

#include <stdbool.h>
#include <stdint.h>


void if_shift_init(void);
void if_shift(void);

void if_shift_setup(bool on, int32_t freq);

#endif //__IF_SHIFT_H
