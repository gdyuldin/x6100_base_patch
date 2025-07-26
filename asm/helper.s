.include "asm/offsets.s"

  .syntax unified
  .cpu cortex-m4
  .fpu fpv4-sp-d16
// .float-abi hard
  .thumb
// -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb

.section .text
.global _start
  nop

/*
   0800060a  20 ee 67 0a    vnmul  param_1,param_1,s15  // param_1 = s0
   0800060e  70 47          bx     lr

*/

/*
*** Configuration block ***

  At begin of the DMA stream handler

   08023c36 09 f0 7b f9     bl      get_dma_cr_register_value (0802cf30)           uint get_dma_cr_regist
   args - r0
   return -r0
*/

.section .insert_to_configure, "ax"
_jump_to_configure_wrapper:
  b _configure_wrapper

.section .configure_wrapper, "ax"
_configure_wrapper:
  bl GET_DMA_CR_REGISTER_VALUE    // call get_dma_cr_register_value

  vpush {s0}
  vpush {s8-s15}
  push {r0-r4, ip, lr}
  bl _configure
  pop {r0-r4, ip, lr}
  vpop {s8-s15}
  vpop {s0}

  b _jump_to_configure_wrapper + 4

.section .configure, "ax"
_configure:
  nop


// Convert IQ to float and apply an offset
/*
   080241ac 68 ee a7 8a     vmul.   s17,s17,s15
   080241b0 28 ee 27 8a     vmul.   s16,s16,s15
*/

.section .insert_to_apply_rx_iq_offset, "ax"
_jump_to_apply_rx_iq_offset_wrapper:
  b _apply_rx_iq_offset_wrapper

.section .apply_rx_iq_offset_wrapper, "ax"
_apply_rx_iq_offset_wrapper:
  // save func registers
  push {r2, r3}
  vpush {s12-s15}

  // push arguments, call func, pop arguments
  vpush {RX_Q_REGISTER}
  vpush {RX_I_REGISTER}
  bl _apply_rx_iq_offset
  vpop {RX_I_REGISTER}
  vpop {RX_Q_REGISTER}

  vpop {s12-s15}
  pop {r2, r3}

  vmul.f32 RX_Q_REGISTER,RX_Q_REGISTER,s15  // from original

  b _jump_to_apply_rx_iq_offset_wrapper + 4

.section .apply_rx_iq_offset, "ax"
_apply_rx_iq_offset:
  nop


// Compressor block

.section .insert_to_compress,"ax"
_jump_to_compress:
  b _compress_wrapper


.section .compress_wrapper,"ax"
_compress_wrapper:
    // bl ARM_FIR_DECIMATE_F32          // arm_fir_decimate_f32
    nop
    nop
    // bl ARM_BIQUAD_CASCADE_DF1_F32    // arm_biquad_cascade_df1_f32
    // vldr.32 s0, [sp, #0x58]  // sp+0x58  tx_audio
    vldr s0, [r1]
    push {r0-r5, ip, lr}
    vpush {s1-s15}

    bl _compress

    vpop {s1-s15}
    pop {r0-r5, ip, lr}
    vstr s0, [r1]

    b _jump_to_compress + 4

.section .compress, "ax"
_compress:
    nop


// Init data block (fill mem with zeros)

.section .insert_to_init_data,"ax"
_jump_to_init_data:
  b _init_data_wrapper


.section .init_data_wrapper, "ax"
_init_data_wrapper:
  bl ORIG_INIT_DATA       // from orig code, SystemInit
  bl _init_data
  b _jump_to_init_data + 4

.section .init_data, "ax"
_init_data:
  nop


// Control tx amp block
/*
   08024b62 54 4c           ldr     IQ_arr_maybe,[DAT_08024cb4]         = 20009940h
   08024b64 22 1f           subs    r2=>FLOAT_ARRAY_2000993c,IQ_arr_ma
   08024b66 a2 f5 96 70     sub.w   nrthr_small_piece_inverted=>DAT_20
   08024b6a 01 23           movs    modulation,#0x1
   08024b6c 18 a9           add     r1,sp,#0x60
   08024b6e 0e f0 af fd     bl      arm_fir_interpolate_f32             void arm_fir_interpola (0x080336d0)

   sp + 0x60 - I signal
   sp + 0x64 - Q signal
*/

.section .insert_to_tx_amp, "ax"
_jump_to_tx_amp_wrapper:
  b _tx_amp_wrapper


.section .tx_amp_wrapper, "ax"
_tx_amp_wrapper:
  // Q signal pointer in r1
  // I signal in sp + 0x64
  push {r0, r3, lr}
  add  r0, sp, #TX_I_SIGNAL_OFFSET + 12
  vpush {s14-s15}
  bl _tx_amp
  vpop {s14-s15}
  pop {r0, r3, lr}
  bl ORIG_TX_AMP      // from orig code, interpolate Q
  b _jump_to_tx_amp_wrapper + 4

.section .tx_amp, "ax"
_tx_amp:
  nop


// tx coeff calc block
/*
  tx power (float) on s0
   080237ae 82 ed 00 0a     vstr.32 param_1,[r2]=>tx_power
   080237b2 d8 60           str     r0,[r3,#0xc]=>FLOAT_2000dac8        = 0.0
   080237b4 19 63           str     r1,[r3,#0x30]=>DAT_2000daec
   080237b6 19 62           str     r1,[r3,#0x20]=>DAT_2000dadc
   080237b8 59 62           str     r1,[r3,#0x24]=>DAT_2000dae0
*/
.section .insert_to_tx_coeff_calc, "ax"
_jump_to_tx_coeff_calc_wrapper:
  b _tx_coeff_calc_wrapper


.section .tx_coeff_calc_wrapper, "ax"
_tx_coeff_calc_wrapper:
  vstr.32 s0, [r2]  // from original code
  push {r0-r4, lr}
  vpush {s0-s16}
  bl _tx_coeff_calc
  vpop {s0-s16}
  pop {r0-r4, lr}
  b _jump_to_tx_coeff_calc_wrapper + 4

.section .tx_coeff_calc, "ax"
_tx_coeff_calc:
  nop


// AM/FM block
/*
  Saving s14 (demoulated signal) to [r2,#0x8]
   08027de0 82 ed 02 7a     vstr.32 s14,[r2,#0x8]=>DAT_20008fa8

  I and Q in 20008138 and 2000813c
  AM demodulated in 20008140

*/

.section .insert_to_am_fm_rx_process, "ax"
_jump_to_am_fm_rx_process_wrapper:
  b _am_fm_rx_process_wrapper

.section .am_fm_rx_process_wrapper, "ax"
_am_fm_rx_process_wrapper:
  // save registers
  vpush {s0}  // 1
  VMOV s0,s14
  vpush {s3-s15} // 13
  push {r0-r5, ip, lr} // 8
  ldrb.w r2, [sp, #RX_SP_OFFSET + ((1 + 13 + 8) * 4)]
  LDR r0, =RX_I_SIGNAL
  LDR r1, =RX_Q_SIGNAL
  bl _am_fm_rx_process
  pop {r0-r5, ip, lr}
  vstr.32 s0,[r2,#0x8]  // Original code
  vpop {s3-s15}
  VMOV s14, s0
  vpop {s0}
  b _jump_to_am_fm_rx_process_wrapper + 4

.section .am_fm_rx_process, "ax"
_am_fm_rx_process:
  nop



// ANF update block
/*
   080251f0 0f f0 bc f8     bl      arm_biquad_cascade_df1_f32          undefined arm_biquad_c
*/

.section .insert_to_anf_update, "ax"
_jump_to_anf_update_wrapper:
  b _anf_update_wrapper

.section .anf_update_wrapper, "ax"
_anf_update_wrapper:
  bl ARM_BIQUAD_CASCADE_DF1_F32     //call arm_biquad_cascade_df1_f32
  // save registers
  push {r2, r3, lr}
  vpush {s10-s15}
  bl _anf_update
  vpop {s10-s15}
  pop {r2, r3, lr}
  b _jump_to_anf_update_wrapper + 4

.section .anf_update, "ax"
_anf_update:
  nop
