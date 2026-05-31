#ifndef __EXTERNAL_H
#define __EXTERNAL_H

#include "stdint.h"
#include "offsets.h"

#include <dsp/filtering_functions.h>

/**
 * OEM variables
*/

#define USE_OEM_MODULATION_AS(x) volatile uint8_t* x = (uint8_t *)MODULATION_ADDR
#define USE_OEM_TX_FLAG_AS(x) volatile uint8_t* x = (uint8_t *)TX_FLAG_VALUE
#define USE_OEM_SQL_AS(x) volatile uint8_t* x = (uint8_t *)SQL_VALUE
#define USE_OEM_FM_DEPTH_OF_MOD_AS(x) volatile float* x = (float *)FM_DEPTH_OF_MOD_VALUE
#define USE_OEM_FREQ_PLUS_RIT_AS(x) volatile uint32_t* x = (uint32_t *)FREQ_PLUS_RIT
#define USE_OEM_SAMPLES_COUNT_VALUE_AS(x) volatile uint32_t* x = (uint32_t *)SAMPLES_COUNT_VALUE
#define USE_OEM_NRE_AS(x) volatile uint8_t* x = (uint8_t *)NRE_FLAG
#define USE_OEM_TX_STATE_FLAGS_AS(x) volatile uint32_t* x = (uint32_t *)TX_STATE_FLAGS
#define USE_OEM_KEY_TONE_AS(x) volatile uint16_t* x = (uint16_t *)KEY_TONE

// I2C registers values start pointer
#define USE_OEM_I2C_REGS_AS(x) volatile uint32_t* x = (uint32_t *)I2C_REGS_ADDR

enum tx_state {
    TX_STATE_IPTT = 1,
    TX_STATE_HPTT = 2,
    // 2 - maybe external mic connected
    TX_STATE_ATU = 0x10,
    TX_STATE_VOX = 0x20,
    TX_STATE_MODEM = 0x40,
    // 0x80 - some gpio (gpioe)
    TX_STATE_CALIBRATION = 0x100,
    TX_STATE_SWR_SCAN = 0x200,
};

/*
            iVar2 = get_cur_tx_maybe();
            if (iVar2 == 0) {
              if ((tx->flags & 2) == 0) {
                if ((tx->flags & 1) != 0) {
                  tx->tx_flag = 1;
                }
              }
              else {
                tx->tx_flag = 1;
              }
            }


            param_1->flags_copy = tx->flags;
            if (((tx->tx_flag == '\x01') && (iVar2 = get_cur_tx_maybe(), iVar2 == 0)) && (tx->flags == 0)) {
                tx->tx_flag = 0;
            }
*/

enum __attribute__((__packed__)) mod_t {
    MOD_LSB,
    MOD_LSB_D,
    MOD_USB,
    MOD_USB_D,
    MOD_CW,
    MOD_CWR,
    MOD_AM,
    MOD_NFM,
};


// Frequencies for RX filters
struct filter_freqs_t {
    int32_t low;
    int32_t high;
};


/**
 * External functions implemented in FW
 */

extern void ext_arm_biquad_cascade_df1_f32 (arm_biquad_casd_df1_inst_f32 *S,float *pSrc,float *pDst, uint32_t blockSize)
    __attribute__((noinline, section(".arm_biquad_cascade_df1_f32_sec")));

extern void ext_arm_fill_f32(float val, float *data, uint32_t size)
    __attribute__((noinline, section(".arm_fill_f32_sec")));

extern void ext_arm_copy_f32(float *pSrc, float *pDst, uint32_t blockSize)
    __attribute__((noinline, section(".arm_copy_f32_sec")));

extern float ext_arm_sin_f32(float val)
    __attribute__((noinline, section(".arm_sin_f32_sec")));

extern float ext_arm_cos_f32(float val)
    __attribute__((noinline, section(".arm_cos_f32_sec")));

extern float ext_sqrt_f32(float val)
    __attribute__((noinline, section(".arm_sqrt_f32_sec")));


extern void ext_arm_fir_decimate_f32(arm_fir_decimate_instance_f32 *S,float *pSrc,float *pDst,uint32_t blockSize)
    __attribute__((noinline, section(".arm_fir_decimate_f32_sec")));

extern void setup_biquad_filter(float sampling_rate, float freq_low, float freq_high, void *flt_S, int param_5)
    __attribute__((noinline, section(".setup_biquad_filter_sec")));


extern void ext_write_i2c(void *i2c_typedef, uint32_t addr, uint8_t *data, uint32_t data_len, uint32_t timeout)
    __attribute__((noinline, section(".write_i2c_sec")));

extern void ext_setup_tx(void *struct_data, uint32_t flags, uint8_t tx)
    __attribute__((noinline, section(".setup_tx_sec")));

extern void ext_setup_internal_mic_power(uint32_t val)
    __attribute__((noinline, section(".setup_internal_mic_power_sec")));

extern void ext_set_mic_level(uint32_t ch, uint32_t val)
    __attribute__((noinline, section(".set_mic_level_sec")));

extern void ext_set_audio_codec_input(uint32_t ch, uint32_t input)
    __attribute__((noinline, section(".set_audio_codec_input_sec")));

#endif // __EXTERNAL_H
