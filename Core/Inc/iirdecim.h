#ifndef __IIRDECIM_H
#define __IIRDECIM_H

#include "external.h"

void iirdecim_init();

void iirdecim_reset(uint8_t fft_dec);

// void iir_decim_iq_2(iq_iir_filter_t *flt, float *pSrc, float *pDst, uint32_t countSrc);
void iir_decim_iq_n(uint8_t N, float *pSrc, float *pDst, uint32_t countSrc);



#endif //__IIRDECIM_H
