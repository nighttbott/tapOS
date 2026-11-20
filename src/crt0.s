/* User entry point. Assemble with gcc -m32, using GNU assembler syntax. */
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    cld
    andl $-16, %esp
    call main

    /* Preserve the M4 exit ABI when main returns. */
    subl $12, %esp
    pushl %eax
    call sys_exit
1:
    jmp 1b
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
