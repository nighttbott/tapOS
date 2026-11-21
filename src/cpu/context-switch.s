; void process_context_switch(const struct Context *context)
; cdecl passes the pointer at [esp+4].
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
%define CTX_EIP      48
%define CTX_EFLAGS   52
%define CTX_CS       56
%define CTX_SS       60
%define CTX_PAGE_DIR 64
%define CTX_SIZE     68

section .text
process_context_switch:
    cli
    mov esi, [esp + 4]

    ; Copy Context before changing CR3: the PCB pointer may not be mapped by
    ; the destination process. Keep the copy on the shared kernel stack.
    cld
    sub esp, CTX_SIZE
    mov edi, esp
    mov ecx, CTX_SIZE / 4
    rep movsd
    mov esi, esp

    ; Context stores a higher-half virtual address; CR3 needs its physical address.
    mov eax, [esi + CTX_PAGE_DIR]
    cmp eax, 0xC0000000
    jb .invalid_context
    sub eax, 0xC0000000
    test eax, 0xFFF
    jnz .invalid_context
    cmp eax, 0x00400000
    jae .invalid_context
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

.invalid_context:
    ; Fail closed rather than load a corrupt CR3 and escalate to #PF/#DF.
    cli
.halt:
    hlt
    jmp .halt
