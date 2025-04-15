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
    // s2, s3, s4, s5, s6, s7, s8 s9, s10, s11, s12, s13, s14, s15
    // r2, r3
    // pointer to tx_audio in r1
    vldr s0, [r1];
    push {r0, r1, r2, r3, r4, ip, lr}
    vpush {s1-s17}
    // move arg to s0
    bl _compressor
    // move res from s0
    // vpop {s0, s1, s12-s16}
    vpop {s1-s17}
    pop {r0, r1, r2, r3, r4, ip, lr}
    vstr s0, [r1];
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
