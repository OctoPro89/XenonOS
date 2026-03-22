section .text
bits 64

extern isr_common_handler

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push 0              ; fake error code
    push %1             ; interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push %1             ; interrupt number
    jmp isr_common_stub
%endmacro

; Exceptions with error codes:
ISR_ERR 8
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_ERR 17

; Everything else:
%assign i 0
%rep 256
%if i != 8 && i != 10 && i != 11 && i != 12 && i != 13 && i != 14 && i != 17
ISR_NOERR i
%endif
%assign i i+1
%endrep

global isr_common_stub
isr_common_stub:
    ; Save registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp    ; pass pointer to register frame

    call isr_common_handler

.hang:
    cli
    hlt
    jmp .hang

section .data
global isr_stub_table

isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep