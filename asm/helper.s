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

// Compressor block

.section .insert_to_tx_process,"ax"
_jump_to_comp:
  b _comp_wrapper


.section .comp_wrapper,"ax"
_comp_wrapper:
    // bl 0x08035ce0  // arm_fir_decimate_f32
    nop
    nop
    // bl 0x0803436c  // arm_biquad_cascade_df1_f32
    // vldr.32 s0, [sp, #0x58]  // sp+0x58  tx_audio
    vldr s0, [r1]
    push {r0-r4, lr}
    vpush {s1-s15}

    bl _compressor

    vpop {s1-s15}
    pop {r0-r4, lr}
    vstr s0, [r1]

    b _jump_to_comp + 4

.section .compressor, "ax"
_compressor:
    nop


// Fill mem block

.section .insert_to_reset_handler,"ax"
_jump_to_fill_mem:
  b _fill_mem_wrapper


.section .fill_mem_wrapper, "ax"
_fill_mem_wrapper:
  bl 0x08032bf8  // from orig code, SystemInit
  bl _fill_mem
  b _jump_to_fill_mem + 4

.section .fill_mem, "ax"
_fill_mem:
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
  add  r0, sp, #0x64 + 12
  vpush {s14-s15}
  bl _tx_amp
  vpop {s14-s15}
  pop {r0, r3, lr}
  bl 0x080336d0  // from orig code, interpolate Q
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
  push {r1-r3, lr}
  vpush {s11-s15}
  bl _tx_coeff_calc
  vpop {s11-s15}
  pop {r1-r3, lr}
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
  ldrb.w r2, [sp, 0x57 + ((1 + 13 + 8) * 4)]
  LDR r0,=0x20008138
  LDR r1,=0x2000813c
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
