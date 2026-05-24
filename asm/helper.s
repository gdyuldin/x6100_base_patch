.include "asm/offsets.s"

  .syntax unified
  .cpu cortex-m4
  .fpu fpv4-sp-d16
// .float-abi hard
  .thumb
// -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb



.macro set_r7_stack
@ Store orig value
  push {r7}
  ldr r7, =CCMRAM_STACK_VAL
.endm

.macro push_c regs:vararg
  STMDB r7!, \regs
.endm

.macro pop_c regs:vararg
  LDMIA r7!, \regs
.endm

.macro vpush_c regs:vararg
  VSTMDB r7!, \regs
.endm

.macro vpop_c regs:vararg
  VLDMIA r7!, \regs
.endm

.macro restore_r7_stack
  pop {r7}
.endm

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

   08023c36 09 f0 7b f9     bl      get_dma_cr_register_value (0802cf30)           uint get_dma_cr_register
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
  vpush {s6-s15}
  push {r0-r3, ip, lr}
  bl _configure

  pop {r0-r3, ip, lr}
  vpop {s6-s15}
  vpop {s0}

  b _jump_to_configure_wrapper + 4

.section .configure, "ax"
_configure:
  nop


// End of the DMA1 Stream3 IRQ handler (for float collecting updates)
/*
  08025b72 4f f4 80 62     mov.w   r2,#0x400
*/

.section .insert_to_dma_end, "ax"
_jump_to_dma_end_wrapper:
  b _dma_end_wrapper

.section .dma_end_wrapper, "ax"
_dma_end_wrapper:

  // save func registers
  vpush {s0}
  vpush {s14-s15}
  push {r0-r3, fp, sl}
  bl _dma_end
  pop {r0-r3, fp, sl}
  vpop {s14-s15}
  vpop {s0}

  mov.w   r2,#0x400  // from original

  b _jump_to_dma_end_wrapper + 4

.section .dma_end, "ax"
_dma_end:
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
  push {r1-r3}
  vpush {s11-s15}

  // push arguments, call func, pop arguments
  vpush {RX_Q_REGISTER}
  vpush {RX_I_REGISTER}
  bl _apply_rx_iq_offset
  vpop {RX_I_REGISTER}
  vpop {RX_Q_REGISTER}

  vpop {s11-s15}
  pop {r1-r3}

  vmul.f32 RX_Q_REGISTER,RX_Q_REGISTER,s15  // from original

  b _jump_to_apply_rx_iq_offset_wrapper + 4

.section .apply_rx_iq_offset, "ax"
_apply_rx_iq_offset:
  nop


// Remove IQ DC offset before processing
/*
   0802492c 41 f8 28 30     str.w   r3,[r1,r8,lsl #offset MEGA_STRUCT.
   ^ insert here
   08024930 d4 ed 00 7a     vldr.32 i_val,[r4]=>data
   08024934 94 ed 01 7a     vldr.32 s14,[r4,#0x4]=>MEGA_STRUCT.iq_data

   int i addr in r4
   int q addr in r4 + 4

*/

.section .insert_to_remove_iq_offset, "ax"
_jump_to_remove_iq_offset_wrapper:
  b _remove_iq_offset_wrapper

.section .remove_iq_offset_wrapper, "ax"
_remove_iq_offset_wrapper:

  // call original
  str.w r3, [r1, r8, lsl #0x2]

  // save func registers
  push {r0-r3, ip, lr}
  vpush {s13-s15}

  // load values
  mov r0, r4
  bl _remove_iq_offset

  vpop {s13-s15}
  pop {r0-r3, ip, lr}

  b _jump_to_remove_iq_offset_wrapper + 4

.section .remove_iq_offset, "ax"
_remove_iq_offset:
  nop


// Compressor block
/*
   08024b18 0f f0 28 fc     bl      arm_biquad_cascade_df1_f32          arm_biquad_cascade_df1_f32(*S, *pSrc, *pDst, cnt)

*/

.section .insert_to_compress,"ax"
_jump_to_compress:
  b _compress_wrapper


.section .compress_wrapper,"ax"
_compress_wrapper:
    // Save pDst to stack
    push {r2}
    bl ARM_BIQUAD_CASCADE_DF1_F32
    // Pop pDst to r0
    pop {r0}

    push {r0-r3}
    vpush {s0-s15}

    bl _compress

    vpop {s0-s15}
    pop {r0-r3}

    b _jump_to_compress + 4

.section .compress, "ax"
_compress:
    nop


// am_modulation
/*
   08028608 a7 ee a6 7a     vfma.   s14,s15,s13

   signal is s15
   carrier_level is s14
   am_level is s13
   result - s14

*/
.section .insert_to_am_modulation,"ax"
_jump_to_am_modulation:
  b _am_modulation_wrapper


.section .am_modulation_wrapper,"ax"
_am_modulation_wrapper:
    vpush {s0-s2}

    vmov.f32 s0, s15
    vmov.f32 s1, s14
    vmov.f32 s2, s13

    vpush {s13-s15}
    push {r3}

    bl _am_modulation

    pop {r3}
    vpop {s13-s15}

    vmov.f32 s14, s0

    vpop {s0-s2}

    b _jump_to_am_modulation + 4

.section .am_modulation, "ax"
_am_modulation:
    nop


// fm_modulate
/*
   08028634 06 f5 da 63     add.w   r3,r6,#0x6d0
  audio in s15

  ....

     08028690 00 ee 10 0a     vmov    s0,r0

    put phase to r0
*/
.section .insert_to_fm_modulate,"ax"
_jump_to_fm_modulate:
  b _fm_modulate_wrapper


.section .fm_modulate_wrapper,"ax"
_fm_modulate_wrapper:
    push {r1-r3}
    vpush {s13-s15}
    vmov.f32    s0, s15

    bl _fm_modulate

    @ Original code
    vmov r0, s0

    vpop {s13-s15}
    pop {r1-r3}
    b 0x08028690

.section .fm_modulate, "ax"
_fm_modulate:
    nop


// Init data block (fill mem with zeros)

.section .insert_to_init_data,"ax"
_jump_to_init_data:
  b _init_data_wrapper


.section .init_data_wrapper, "ax"
_init_data_wrapper:
  @ cpsid i  @ disable IRQ
  @ ldr r0, =CCMRAM_STACK_MIN
  @ MSR msplim, r0
  @ MRS     R0, CONTROL
  @ BIC     R0, R0, #(1 << 1)  @ clear SPSEL for MSP
  @ BIC     R0, R0, #(1 << 0)  @ clear nPRIV for privileged thread mode
  @ MSR     CONTROL, R0
  @ isb

  @ ldr r0, =CCMRAM_STACK_VAL
  @ MSR msp, r0
  @ isb
  @ cpsie i  @ enable IRQ


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
   08024b6e 0e f0 af fd     bl      arm_fir_interpolate_f32             void arm_fir_interpolation (0x080336d0)

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
  push {r0-r3, lr}
  vpush {s0-s15}
  bl _tx_coeff_calc
  vpop {s0-s15}
  pop {r0-r3, lr}
  b _jump_to_tx_coeff_calc_wrapper + 4

.section .tx_coeff_calc, "ax"
_tx_coeff_calc:
  nop



// FM demodulation
/*
   08028b56 f9 f7 29 f8     bl      FM_demod_maybe                      void FM_demod_maybe(*S, float[2] iq, float res*, n)
*/

.section .insert_to_fm_demodulate, "ax"
_jump_to_fm_demodulate_wrapper:
  bl _fm_demodulate

.section .fm_demodulate_wrapper, "ax"
_fm_demodulate_wrapper:
  nop
/*
  vstr.32 s0, [r2]  // from original code
  push {r0-r4, lr}
  vpush {s0-s16}
  bl _fm_demodulate
  vpop {s0-s16}
  pop {r0-r4, lr}
  b _jump_to_fm_demodulate_wrapper + 4
*/

.section .fm_demodulate, "ax"
_fm_demodulate:
  nop

// AM/FM block

/*
  Saving s14 (demodulated signal) to [r2,#0x8]
   08027de0 82 ed 02 7a     vstr.32 s14,[r2,#0x8]=>DAT_20008fa8

  I and Q in 20008138 and 2000813c
  AM demodulated in 20008140

*/


.section .insert_to_am_fm_rx_process, "ax"
_jump_to_am_fm_rx_process_wrapper:
  b _am_fm_rx_process_wrapper

.section .am_fm_rx_process_wrapper, "ax"
_am_fm_rx_process_wrapper:
  // save registers, load_data
  vpush {s0-s1}
  vpush {s13-s15}
  push {r0, r2}

  bl _am_fm_rx_process

  pop {r0, r2}
  vpop {s13-s15}
  vpop {s0-s1}

  // load processed demodulated value
  push {r3}
  ldr     r3,=AM_FM_DEMOD
  vldr.32 s14,[r3]
  pop {r3}

  vstr.32 s14,[r2,#0x8]  // Original code

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
  push {r2, r3}
  vpush {s10-s15}
  bl _anf_update
  vpop {s10-s15}
  pop {r2, r3}
  b _jump_to_anf_update_wrapper + 4

.section .anf_update, "ax"
_anf_update:
  nop


/*
*** Copy flow block ***

  Called with TIM, used to send adjacent samples

   08033c88 ee f7 aa fd     bl      copy_flow_samples_to_arg            uint32_t copy_flow_sam
   args - r0
   return -r0 (ignored)
*/

.section .insert_to_copy_flow, "ax"
_jump_to_copy_flow_wrapper:
  bl _copy_flow

.section .copy_flow_wrapper, "ax"
_copy_flow_wrapper:
  nop

.section .copy_flow, "ax"
_copy_flow:
  nop

/*
*** Process MAIN i2c custom commands ***

  Inject a process for unused i2c cmd to handle custom commands.

   0802c1a0 01 f0 ee fa     bl      get_battery_data_maybe              undefined get_battery_
   no args
   no return
*/
.section .insert_to_process_i2c_cmd, "ax"
_jump_to_process_i2c_cmd_wrapper:
  b _process_i2c_cmd_wrapper

.section .process_i2c_cmd_wrapper, "ax"
_process_i2c_cmd_wrapper:
  bl GET_BATTERY_DATA_MAYBE     //call get_battery_data_maybe
  push {r0-r3, ip, lr}
  vpush {s0-s2}
  vpush {s6-s15}
  bl _process_i2c_cmd
  vpop {s0-s2}
  vpop {s6-s15}
  pop {r0-r3, ip, lr}
  b _jump_to_process_i2c_cmd_wrapper + 4

.section .process_i2c_cmd, "ax"
_process_i2c_cmd:
  nop


/*
*** Apply IF shift before demodulation ***


   08024ac8 9d f8 07 31     ldrb.w  r3,[sp,#local_79] ([sp,#0x107])

*/
.section .insert_to_if_shift_rx, "ax"
_jump_to_if_shift_rx_wrapper:
  b _if_shift_rx_wrapper

.section .if_shift_rx_wrapper, "ax"
_if_shift_rx_wrapper:
  push {r0-r3, lr}
  vpush {s11-s15}
  bl _if_shift_rx
  vpop {s11-s15}
  pop {r0-r3, lr}

  ldrb.w r3, [sp,#0x107]  // Call original code

  b _jump_to_if_shift_rx_wrapper + 4

.section .if_shift_rx, "ax"
_if_shift_rx:
  nop


/*
*** Apply IF shift on TX ***


   0802d320 7b 61           str     r3,[r7,#0x14]
   0802d322 2a e0           b       0x0802d37a

*/
.section .insert_to_if_shift_tx, "ax"
_jump_to_if_shift_tx_wrapper:
  b _if_shift_tx_wrapper

.section .if_shift_tx_wrapper, "ax"
_if_shift_tx_wrapper:
  // Save r0, move r3 to fn arg
  push {r0, r2}
  mov r0, r3
  bl _if_shift_tx
  // Move result to r3, restore r0
  mov r3, r0
  pop {r0, r2}

  str r3, [r7, #0x14] // Call original code
  b 0x0802d37a

.section .if_shift_tx, "ax"
_if_shift_tx:
  nop


/*
*** Noise reduction ***
    08024bf6 04 f5 f4 74     add.w   r4,r4,#0x1e8
    input signal is in s15
*/
.section .insert_to_nr_apply, "ax"
_jump_to_nr_apply_wrapper:
  b _nr_apply_wrapper

.section .nr_apply_wrapper, "ax"
_nr_apply_wrapper:

  @ ldr r0, [sp, #0x10]
  @ vpush {s0-s14}
  @ push {r1-r3, ip}

  push {r0-r3, ip, lr}
  VMRS R0, FPSCR
  PUSH {R0}
  vpush {s0-s14}
  VMOV s0, s15
  bl _nr_apply
  VMOV s15, s0
  vpop {s0-s14}
  POP {R0}
  VMSR FPSCR, R0
  pop {r0-r3, ip, lr}

  @ pop {r1-r3, ip}
  @ vpop {s0-s15}
  @ Replaced code
  add.w   r4,r4,#0x1e8

  b _jump_to_nr_apply_wrapper + 4

.section .nr_apply, "ax"
_nr_apply:
  nop

.section .insert_to_skip_oem_nr, "ax"
_jump_to_skip_oem_nr_wrapper:
  b AFTER_OEM_NR_ADDR

/*
   08024da8 00 2c           cmp     r4,#0x0
   08024daa 41 f0 f6 83     bne.w   LAB_0802659a
 */
.section .insert_to_skip_oem_nr_postprocess, "ax"
_jump_to_skip_oem_nr_postprocess_wrapper:
  @ skip instruction
  NOP
  NOP
  NOP


/*
Skip AM demod multiplication
*/

.section .insert_to_skip_am_mult, "ax"
_skip_am_mult_wrapper:
  b AFTER_OEM_AM_MULT_ADDR


/**
Noise blanker

   08024962 8a ed 00 ba     vstr.32 s22,[r10]=>MEGA_STRUCT.iq_data.i_f
   08024966 8b ed 00 8a     vstr.32 s16,[r11]=>MEGA_STRUCT.iq_data.q

 */

.section .insert_to_nb_apply, "ax"
_jump_to_nb_apply_wrapper:
  b _nb_apply_wrapper

.section .nb_apply_wrapper, "ax"
_nb_apply_wrapper:
  push {r0-r3, r12}
  vpush {s11-s15}
  vpush {s0-s1}

  vmov s0, s16
  vmov s1, s22
  bl _nb_apply
  vmov s16, s0
  vmov s22, s1

  vpop {s0-s1}
  vpop {s11-s15}
  pop {r0-r3, r12}

  vstr.32 s22,[r10]  // Call original instruction

  b _jump_to_nb_apply_wrapper + 4

.section .nb_apply, "ax"
_nb_apply:
  nop


/**
SSB IQ filter setup

   08026f92 4f f4 80 43     mov.w   r3 ,#0x4000  // 2 bytes for 3300.0 float32
   08026f96 9b ed 00 0a     vldr.32 s0,[r11]
   08026f9a c4 f2 4e 53     movt    r3, #0x454e  // 2 bytes for 3300.0 float32
*/

.section .insert_to_ssb_iq_filter1, "ax"
  @ old value: 3300.0 (0x454e4000)
  @ new value: 5000.0 (0x459c4000)
  mov.w   r3 ,#0x4000
  vldr.32 s0,[r11]
  movt    r3, #0x459c

/**
   08026fbe 4f f4 80 43     mov.w   r3,#0x4000
   08026fc2 c4 f2 ce 53     movt    r3,#0x45ce
 */
.section .insert_to_ssb_iq_filter2, "ax"
  @ old value: 6600.0 (0x45ce4000)
  @ new value: 10000.0 (0x461c4000)
  mov.w   r3 ,#0x4000
  movt    r3, #0x461c


/**

Setup AIC IIR high pass filter(DC blocker for ADC)
   0802fc6a 04 f0 cb fe     bl      sleep_maybe                         undefined sleep_maybe()

*/

.section .insert_to_aic_setup_adc_dc_blocker, "ax"
_jump_to_aic_setup_adc_dc_blocker_wrapper:
  b _aic_setup_adc_dc_blocker_wrapper

.section .aic_setup_adc_dc_blocker_wrapper, "ax"
_aic_setup_adc_dc_blocker_wrapper:

  @ Original code
  bl 0x08034a04

  bl _aic_setup_adc_dc_blocker

  b _jump_to_aic_setup_adc_dc_blocker_wrapper + 4

.section .aic_setup_adc_dc_blocker, "ax"
_aic_setup_adc_dc_blocker:
  nop

@ sl -> R10
@ fp -> R11
@ ip -> R12
@ sp -> R13
@ lr -> R14
@ pc -> R15

