#ifndef __CW_PEAK
#define __CW_PEAK

#include "stdint.h"
#include "stdbool.h"

void cw_peak_init(void);
void cw_peak_setup(bool on, float Q);
float cw_peak_process(float sample);

#endif //__CW_PEAK
