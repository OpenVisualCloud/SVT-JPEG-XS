;
; Copyright(c) 2024 Intel Corporation
; SPDX - License - Identifier: BSD - 2 - Clause - Patent
;

%include "x64inc.asm"
section .text

; ----------------------------------------------------------------------------------------
cglobal gc_precinct_stage_scalar_loop_ASM
.loop:
    mov r3w, [r1]    ;coeff_data_ptr_16bit[0];
    or r3w, [r1+2]   ;coeff_data_ptr_16bit[1];
    or r3w, [r1+4]   ;coeff_data_ptr_16bit[2];
    or r3w, [r1+6]   ;coeff_data_ptr_16bit[3];
    shl r3w,1
    or r3w,1
    bsr r4w, r3w
    mov [r2], r4b

  inc r2
  add r1, 8 ; GROUP_SIZE * sizeof(uint16_t)
  dec r0
  jnz .loop

  ret

cglobal msb_x16_ASM
    mov [RSP-8], R8
    mov [RSP-16], R9
    mov [RSP-24], R10
    mov [RSP-32], R11

    bsr R8W, [r0]
    bsr R9W, [r0+2]
    bsr R10W, [r0+4]
    bsr R11W, [r0+6]
    mov [r1], R8B
    mov [r1+1], R9B
    mov [r1+2], R10B
    mov [r1+3], R11B
    add r1, 4
    add r0, 8

    bsr R8W, [r0]
    bsr R9W, [r0+2]
    bsr R10W, [r0+4]
    bsr R11W, [r0+6]
    mov [r1], R8B
    mov [r1+1], R9B
    mov [r1+2], R10B
    mov [r1+3], R11B
    add r1, 4
    add r0, 8

    bsr R8W, [r0]
    bsr R9W, [r0+2]
    bsr R10W, [r0+4]
    bsr R11W, [r0+6]
    mov [r1], R8B
    mov [r1+1], R9B
    mov [r1+2], R10B
    mov [r1+3], R11B
    add r1, 4
    add r0, 8

    bsr R8W, [r0]
    bsr R9W, [r0+2]
    bsr R10W, [r0+4]
    bsr R11W, [r0+6]
    mov [r1], R8B
    mov [r1+1], R9B
    mov [r1+2], R10B
    mov [r1+3], R11B

    mov R8, [RSP-8]
    mov R9, [RSP-16]
    mov R10, [RSP-24]
    mov R11, [RSP-32]
  ret
