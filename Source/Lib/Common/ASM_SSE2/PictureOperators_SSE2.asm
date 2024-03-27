;
; Copyright(c) 2024 Intel Corporation
; SPDX - License - Identifier: BSD - 2 - Clause - Patent
;

%include "x64inc.asm"
section .text

; ----------------------------------------------------------------------------------------
    cglobal Log2_32_ASM
;   If (r0 == 0) then bsr return undefined behavior. For 0 return 0
    or r0, 1
    bsr rax, r0
    ret
