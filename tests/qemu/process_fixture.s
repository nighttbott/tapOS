bits 32
section .text.entry progbits alloc exec nowrite align=16
global _start
_start:
    jmp _start

section .data
    dd 0x12345678

section .bss
    resd 1

section .note.GNU-stack noalloc noexec nowrite progbits
