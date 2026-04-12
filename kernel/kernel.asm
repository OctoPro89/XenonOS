section .text

extern kernel_main_trampoline
extern kernel_stack_top

global kernel_start

kernel_start:
    ; switch to kernel stack
    mov rsp, [rel kernel_stack_top]
    xor rbp, rbp

    ; now safe to call C code
    ; BootInfo* in RDI from bootloader
    call kernel_main_trampoline

.hang:
    cli
    hlt
    jmp .hang