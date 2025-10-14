#ifndef __OFFSETS_H
#define __OFFSETS_H

#define STACK_ADDR              (0x20030000)

#if BUILD_DATE == 221112001

#define MODULATION_ADDR                 (0x2000a8cd)
#define CMP_ENABLED_VALUE               (0x200000c4)
#define CMP_LEVEL_VALUE                 (0x200000c3)
#define SQL_VALUE                       (0x200000a9)
#define FLOW_RESERVED_ADDR              (0x200013f8)
#define I2C_REGS_ADDR                   (0x2000357c)
#define TX_FLAG_VALUE                   (0x2000a8cf)
#define AM_CARRIER_LEVEL_VALUE          (0x2000a174)
#define INPUT_RF_SIGNAL_INT_ADDR        (0x200003b0)
#define SAMPLES_COUNT_VALUE             (0x2000dd58)
#define AM_DEPTH_OF_MOD_VALUE           (0x2000a178)
#define FM_DEPTH_OF_MOD_VALUE           (0x2000a184)
#define ARM_FIR_DECIMATE_INSTANCE_ADDR  (0x20008e74)
#define VAL_ACC_ARRAY                   (0x20008fa8)
#define ARM_BIQUAD_CASD_DF1_INST_VALUE  (0x2000d994)
#define TX_SAMPLING_RATE_12_5           (0x2000dd64)
#define BIQUAD_TX_FLT_INST              (0x20009aa8)
#define STOP_COPY_FLOW                  (0x20007128)
#define FREQ_PLUS_RIT                   (0x200000f0)
#define FLOW_SAMPLES_COUNTER            (0x2000712c)
#define FLOW_SAMPLES_CPLX               (0x20007130)
#define FLOW_N_SAMPLES                  (0x2000dd5c)

// uint32_t copy_flow_samples_to_arg(float *p_Dst) - 08022658

// x6100_flow - 200003e4

// To replace with custom FN
//    08032128 f0 f7 96 fa     bl      copy_flow_samples_to_arg            uint32_t copy_flow_sam


#elif BUILD_DATE == 230307001

#define MODULATION_ADDR                 (0x2000a8d5)
#define CMP_ENABLED_VALUE               (0x200000c4)
#define CMP_LEVEL_VALUE                 (0x200000c3)
#define SQL_VALUE                       (0x200000a9)
#define FLOW_RESERVED_ADDR              (0x20001400)
#define I2C_REGS_ADDR                   (0x20003584)
#define TX_FLAG_VALUE                   (0x2000a8d7)
#define AM_CARRIER_LEVEL_VALUE          (0x2000a17c)
#define INPUT_RF_SIGNAL_INT_ADDR        (0x200003b8)
#define SAMPLES_COUNT_VALUE             (0x2000dd60)
#define AM_DEPTH_OF_MOD_VALUE           (0x2000a180)
#define FM_DEPTH_OF_MOD_VALUE           (0x2000a18c)
#define ARM_FIR_DECIMATE_INSTANCE_ADDR  (0x20008e7c)
#define VAL_ACC_ARRAY                   (0x20008fb0)
#define ARM_BIQUAD_CASD_DF1_INST_VALUE  (0x2000d99c)
#define TX_SAMPLING_RATE_12_5           (0x2000dd6c)
#define BIQUAD_TX_FLT_INST              (0x20009ab0)
#define STOP_COPY_FLOW                  (0x20007130)
#define FREQ_PLUS_RIT                   (0x200000f0)
#define FLOW_SAMPLES_COUNTER            (0x20007134)
#define FLOW_SAMPLES_CPLX               (0x20007138)
#define FLOW_N_SAMPLES                  (0x2000dd64)


// uint32_t copy_flow_samples_to_arg(float *p_Dst) - 080227e0

// flow_samples_cplx - 20007138

// uint32_t_2000dd5c_512 - 2000dd64

// x6100_flow - 200003ec

// To replace with custom FN
//    08033c88 ee f7 aa fd     bl      copy_flow_samples_to_arg            uint32_t copy_flow_sam


#endif

#endif
