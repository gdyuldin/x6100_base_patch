#ifndef __AIC3204_H
#define __AIC3204_H

#include "stdint.h"

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

/**
 * Incompleted struct for handle AIC-related values
 */
struct aic_data_s {
    uint32_t ors;
    uint32_t bitrate;
    uint32_t mod_dac_adc_clk;
    uint32_t dac_adc_clk;
    uint32_t PLL_out_freq;
    uint32_t txco;
    uint32_t sample_rate;
    uint32_t word_width_mb;
    uint32_t channels_num_mb;
    uint32_t BCLK_N_divider_8;
};

void aic_setup_adc_dc_blocker(void);

void aic_sleep(void);

/**
 * Setup PLL and cloak registers for AIC chip with more accurate values.
 */
uint32_t aic_pll_setup(struct aic_data_s *aic_data, uint32_t sample_rate);

#endif //__AIC3204_H
