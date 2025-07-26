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
#define INPUT_DATA_ADDR                 (0x200003b0)
#define SAMPLES_COUNT_VALUE             (0x2000dd58)
#define AM_DEPTH_OF_MOD_VALUE           (0x2000a178)
#define FM_DEPTH_OF_MOD_VALUE           (0x2000a184)
#define ARM_FIR_DECIMATE_INSTANCE_ADDR  (0x20008e74)
#define VAL_ACC_ARRAY                   (0x20008fa8)
#define ARM_BIQUAD_CASD_DF1_INST_VALUE  (0x2000d994)

#define USE_MATH_SQRT
#define TX_AMP_COEFFS_MULTIPLIER        (1.0f)

#elif BUILD_DATE == 230307001

#define MODULATION_ADDR                 (0x2000a8d5)
#define CMP_ENABLED_VALUE               (0x200000c4)
#define CMP_LEVEL_VALUE                 (0x200000c3)
#define SQL_VALUE                       (0x200000a9)
#define FLOW_RESERVED_ADDR              (0x20001400)
#define I2C_REGS_ADDR                   (0x20003584)
#define TX_FLAG_VALUE                   (0x2000a8d7)
#define AM_CARRIER_LEVEL_VALUE          (0x2000a17c)
#define INPUT_DATA_ADDR                 (0x200003b8)
#define SAMPLES_COUNT_VALUE             (0x2000dd60)
#define AM_DEPTH_OF_MOD_VALUE           (0x2000a180)
#define FM_DEPTH_OF_MOD_VALUE           (0x2000a18c)
#define ARM_FIR_DECIMATE_INSTANCE_ADDR  (0x20008e7c)
#define VAL_ACC_ARRAY                   (0x20008fb0)
#define ARM_BIQUAD_CASD_DF1_INST_VALUE  (0x2000d99c)


//#define USE_MATH_SQRT
// this firmware version's output is too low
// 0.1w out on 5w device setting
#define TX_AMP_COEFFS_MULTIPLIER        (9.5f)

#endif

#endif
