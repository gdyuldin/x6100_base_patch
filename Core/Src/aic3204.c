#include "aic3204.h"
#include "external.h"

#include "stdint.h"

#define I2C2_ADDR 0x40005800
#define AIC_I2C_ADDR 0x30


void aic_setup_adc_dc_blocker(void)
{

    void *i2c2 = (void*)I2C2_ADDR;

    // First order IIR - high pass filter with 50 Hz.
    // scipy:  b, a = signal.iirfilter(1, 50 * 2 / 100_000, btype='highpass', ftype='butter', output='ba')
    // b - numerator
    // a - denominator (invert sign)
    // 24, 28, 32 - registers for a ADC left channel
    uint8_t n0[] = {24, 0x7f, 0xcc, 0x9b};
    uint8_t n1[] = {28, 0x80, 0x33, 0x64};
    uint8_t d1[] = {32, 0x7f, 0x99, 0x37};

    uint8_t msg[2];

    // Switch to page 8
    msg[0] = 0;
    msg[1] = 8;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, msg, 2, 10000);


    // Write values for left channel
    ext_write_i2c(i2c2, AIC_I2C_ADDR, n0, 4, 10000);
    ext_write_i2c(i2c2, AIC_I2C_ADDR, n1, 4, 10000);
    ext_write_i2c(i2c2, AIC_I2C_ADDR, d1, 4, 10000);

    // Switch to page 9
    msg[0] = 0;
    msg[1] = 9;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, msg, 2, 10000);

    // Write values for right channel
    n0[0] = 32;
    n1[0] = 36;
    d1[0] = 40;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, n0, 4, 10000);
    ext_write_i2c(i2c2, AIC_I2C_ADDR, n1, 4, 10000);
    ext_write_i2c(i2c2, AIC_I2C_ADDR, d1, 4, 10000);
}


void aic_sleep(void) {
    void *i2c2 = (void*)I2C2_ADDR;

    // Switch to page 0
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){0, 0}, 2, 10000);

    // Mute and power down both DAC channels
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){82, 0}, 2, 10000);

    // Mute and power down both ADC channels
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){81, 0}, 2, 10000);

    // Switch to page 1
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){0, 1}, 2, 10000);

    // Power down Headphone driver
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){16, 0}, 2, 10000);

    // // Power down Line-out driver
    // ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){18, 0}, 2, 10000);

    // // Power down internal LDO and Reference
    // ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){2, 0}, 2, 10000);
}

uint32_t aic_pll_setup(struct aic_data_s *aic_data, uint32_t sample_rate) {
    void *i2c2 = (void*)I2C2_ADDR;
    uint8_t data[7];

    // Select AOSR and DOSR
    // ---
    // ADC_MOD_CLK = AOSR × ADC_FS
    // ADC_MOD_CLK ≤ 6.758 MH
    // ---
    // osr = 64
    // ADC_MOD_CLK = 6.4 Mhz

    // Select MADC and MDAC
    // ---
    // ADC_CLK = MADC × ADC_MOD_CLK
    // (MADC × AOSR) / 32 ≥ RC
    // ---
    // RC PRB_P25 = 12
    // RC PRB_R1 = 6
    // madc = 8
    // 8*64 / 32 = 16
    // ADC_CLK = 51.2 MHz

    // Select NADC and NDAC
    // ---
    // CODEC_CLKIN = NADC × ADC_CLK = NDAC × DAC_CLK
    // CODEC_CLK ≤ 137 MHz
    // ---
    // nadc = 2
    // CODEC_CLKIN = 102.4

    // Use a fixed values
    uint32_t osr = 64;
    // NDAC=2, MDAC=4
    uint32_t ndac_adc = 2;
    uint32_t mdac_adc = 8;

    uint32_t pll_p = aic_data->txco / 10000000;
    uint32_t pll_r = 1;

    // CODEC_CLKIN = NADC * MADC * AOSR * ADC_FS
    float codec_clkin = sample_rate * osr * ndac_adc * mdac_adc;
    float pll_jd = (float)codec_clkin * pll_p / (aic_data->txco * pll_r);
    uint32_t pll_j = (uint32_t)pll_jd;
    uint32_t pll_d = (pll_jd - pll_j) * 10000;
    // PLL_CLK = PLL_CLKIN * R * J.D / P
    // PLL_CLKIN = 26 MHz (mclk)
    uint32_t pll_out_freq = aic_data->txco * pll_r * pll_jd / pll_p;

    aic_data->sample_rate = sample_rate;
    aic_data->ors = osr;
    aic_data->bitrate = aic_data->word_width_mb * sample_rate * aic_data->channels_num_mb;
    aic_data->PLL_out_freq = pll_out_freq;
    aic_data->dac_adc_clk = pll_out_freq / ndac_adc;
    aic_data->mod_dac_adc_clk = aic_data->dac_adc_clk / mdac_adc;
    aic_data->BCLK_N_divider_8 = aic_data->dac_adc_clk / (aic_data->word_width_mb * aic_data->channels_num_mb * aic_data->sample_rate);

    // Switch to page 0
    ext_write_i2c(i2c2, AIC_I2C_ADDR, (uint8_t []){0, 0}, 2, 10000);

            /* Reg 4, clock set */
    data[0] = 4;
            /* High PLL Clock Range */
    data[1] = 0x40;
            /* PLL R=1, P=pll_p, PLL power up */
    data[2] = (uint8_t)(pll_p << 4) | 0x81;
            /* PLL divider J value = 15 */
    data[3] = (uint8_t)pll_j;
            /* PLL divider D value (MSB) */
    data[4] = (uint8_t)((uint16_t)pll_d >> 8);
            /* PLL divider D value (LSB) */
    // data[5] = 8;
    data[5] = (uint8_t)pll_d;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, data, 6, 10000);

            /* dac ocr set DOSR (128) */
    data[0] = 0xd;
    data[1] = 0;
    data[2] = (uint8_t)osr;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, data, 3, 10000);

            /* ADC Oversampling (AOSR 128) */
    data[0] = 0x14;
    data[1] = (uint8_t)osr;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, data, 2, 10000);

            /* Clock Setting Register 6, NDAC Values, NDAC=2, MDAC=4 */
    data[0] = 0xb;
    data[1] = (uint8_t)ndac_adc | 0x80;
    data[2] = (uint8_t)mdac_adc | 0x80;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, data, 3, 10000);

    /* Clock Setting Register 8, NADC Values
                NADC divider powered down, ADC_CLK is same as DAC_CLK
                MADC divider powered down, ADC_MOD_CLK is same as DAC_MOD_CLK */
    data[0] = 0x12;
    data[1] = (uint8_t)ndac_adc;
    data[2] = (uint8_t)mdac_adc;
    ext_write_i2c(i2c2, AIC_I2C_ADDR, data, 3, 10000);
    return 1;
}


// cs4272 pinout
/*
CS4272 slave mode

MCLK (3, in) - 25.6 ? MHz from AIC (MISO/MFP4)
SCLK (5, in) - 6.4 Mhz from AIC (BCLK)
LRCLK (4, in) - 100 kHz from AIC (WCLK)


SDA/CDIN (12) - 1
SCL/CCLK (11) - 1
AD0/CS (13) is on ground, perhaps i2c mode

I2C addr - 0x20 - for write, 0x21 - for read

GPIO PG14 - RST (14) pin (on low)

*/
