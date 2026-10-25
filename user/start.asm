[bits 64]

section .text
global _start

extern main
extern exit_process

_start:
    ; at ELF entry:
    ;   [rsp]       = argc
    ;   [rsp + 8]   = argv[0]
    ;   ...
    ;   argv[argc]  = NULL
    ;
    ; the initial RSP is 16-byte aligned

    xor ebp, ebp

    ; First C argument: argc
    mov edi, dword [rsp]

    ; Second C argument: argv
    lea rsi, [rsp + 8]

    ; align RSP before the CALL
    ; at entry, RSP is already 16-byte aligned, so this is
    ; normally a no-op, keeping it explicit documents the ABI
    and rsp, -16

    call main

    ; main's return value is in EAX
    mov edi, eax
    call exit_process

    ud2