#ifndef __AIC3204_H
#define __AIC3204_H

// AIC in channels
#define AIC_LEFT_CH 1
#define AIC_RIGHT_CH 2

// AIC outputs:
// HPL - MAIN board output
// HPR - speaker/phones
// LOL - USB card
// LOR - USB card

// AIC inputs
// IN_1_L - imic
// IN_2_L - hmic
// IN_2_R - line_in (USB card)
// IN_3_R - line in (MAIN board) (modem_trx - turn on)

#define AIC_IN_1 1  // Input for internal mic
#define AIC_IN_2 2  // Line in from CMedia usb sound card
#define AIC_IN_3 4  // connected to the main board

void aic_setup_adc_dc_blocker(void);

#endif //__AIC3204_H
