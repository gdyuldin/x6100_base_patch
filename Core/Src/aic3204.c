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
