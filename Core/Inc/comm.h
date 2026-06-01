#ifndef __COMM_H
#define __COMM_H

#include "stdint.h"
#include "stdbool.h"

#define I2C_REG_T(fields) typedef union {uint32_t i; struct fields v;}

// Flow complex samples size
#define FLOW_SEQ_SAMPLES 512
#define CFLOAT32_BYTES (sizeof(float) * 2)
#define CFLOAT16_BYTES (sizeof(uint16_t) * 2)

/**
 * I2C registers
 */
typedef enum
{
    x6100_sple_atue_trx = 12,
    x6100_vi_vm,
    x6100_rxvol,
    x6100_rfg_txpwr = 15,  // plus fft span
    x6100_flow_fm_emp = 18,
    x6100_dac_adc_offsets = 19,
    x6100_voxg_voxag_voxdly_voxe = 22,
    x6100_nrthr_nbw_nbthr_nre_nbe = 23,
    x6100_dnfcnt_dnfwidth_dnfe = 24,
    x6100_cmplevel_cmpe = 25,
    x6100_if_shift = 35,
    x6100_cw_peak,
    x6100_tx_filter = 38,

    x6100_filter1_low = 44,
    x6100_filter1_high,
    x6100_filter2_low,
    x6100_filter2_high,
} x6100_cmd_enum_t;

/* Reg x6100_sple_atue_trx */

enum
{
    x6100_sple = 0x00002,
    x6100_voice_rec = 0x00008,
    x6100_swrscan_trx = 0x00010,
    x6100_atue = 0x01000,
    x6100_atu_tune = 0x02000,
    x6100_modem_trx = 0x04000,
    x6100_calibration_trx = 0x08000,
    x6100_power_off = 0x10000,
    x6100_iptt = 0x40000
};

typedef enum __attribute__((__packed__))
{
    x6100_flow_fp32 = 0,
    x6100_flow_bf16,
} x6100_flow_fmt_t;


I2C_REG_T({
    uint8_t flow_fp16: 1;
    uint8_t fm_emp: 1;
}) x6100_reg_flow_fmt_fm_emp_t;

I2C_REG_T({
    uint16_t adc_dac_gain_offset;  // bf16
    uint16_t dac_gain_offset;  // bf16
}) x6100_reg_dac_adc_offsets_t;

I2C_REG_T({
    uint16_t low;
    uint16_t high;
}) x6100_reg_tx_filter_t;

I2C_REG_T({
    uint8_t gain : 7;
    uint8_t ag : 7;
    uint16_t delay : 14;
    bool on : 1;
}) x6100_reg_vox_t;

I2C_REG_T({
    uint8_t nr_level;
    uint8_t nb_width;
    uint8_t nb_level;
    uint8_t nre : 1;
    uint8_t nbe : 1;
}) x6100_reg_nrthr_nbw_nbthr_nre_nbe_t;

I2C_REG_T({
    uint8_t on: 1;
    uint8_t q: 7;
    uint8_t _byte2;
    uint8_t _byte3;
    uint8_t _byte4;
}) x6100_reg_cw_peak_t;


typedef struct __packed {
    uint32_t lo_freq;
    uint8_t flow_fmt;
    uint8_t flow_seq_n: 4;
    uint8_t flow_seq_total: 4;
    uint8_t vary_freq: 1;
    uint8_t fft_dec: 3;
    uint16_t audio_in_lvl_db: 7;  // audio in dB in range 0 ... 127 with +127 dB offset
    uint32_t _pad1: 5;
    uint32_t _pad2;
} flow_info_t;

void comm_init(void);

void set_flow_params(x6100_flow_fmt_t fmt);
void flow_collecting_at_end(void);
uint8_t get_rx_vol(void);


#endif //__COMM_H
