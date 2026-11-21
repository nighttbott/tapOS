; void process_context_switch(struct Context context)
; cdecl passes the structure by value at [esp+4].
BITS 32
global process_context_switch

%define CTX_EDI       0
%define CTX_ESI       4
%define CTX_EBP       8
%define CTX_USER_ESP 12
%define CTX_EBX      16
%define CTX_EDX      20
%define CTX_ECX      24
%define CTX_EAX      28
%define CTX_GS       32
%define CTX_FS       36
%define CTX_ES       40
%define CTX_DS       44
%define CTX_EIP      64
%define CTX_EFLAGS   68
%define CTX_CS       72
%define CTX_SS       76
%define CTX_PAGE_DIR 80

section .text
process_context_switch:
    cli
    mov esi, [esp + 4]

    ; Context stores a higher-half virtual address; CR3 needs its physical address.
    mov eax, [esi + CTX_PAGE_DIR]
    sub eax, 0xC0000000
    mov cr3, eax

    ; Prepare the privilege-return frame on the shared kernel interrupt stack.
    push dword [esi + CTX_SS]
    push dword [esi + CTX_USER_ESP]
    push dword [esi + CTX_EFLAGS]
    push dword [esi + CTX_CS]
    push dword [esi + CTX_EIP]

    mov ax, [esi + CTX_DS]
    mov ds, ax
    mov ax, [esi + CTX_ES]
    mov es, ax
    mov ax, [esi + CTX_FS]
    mov fs, ax
    mov ax, [esi + CTX_GS]
    mov gs, ax

    mov edi, [esi + CTX_EDI]
    mov ebp, [esi + CTX_EBP]
    mov ebx, [esi + CTX_EBX]
    mov edx, [esi + CTX_EDX]
    mov ecx, [esi + CTX_ECX]
    mov eax, [esi + CTX_EAX]
    mov esi, [esi + CTX_ESI]
    iretd
