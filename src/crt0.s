bits 32
section .text.entry progbits alloc exec nowrite align=16
global _start:function
extern main
extern sys_exit

_start:
    cld
    and esp, -16
    call main

    sub esp, 12
    push eax
    call sys_exit
.hang:
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
