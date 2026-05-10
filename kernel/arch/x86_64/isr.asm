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

; TODO: this may not suffice later on with FPU/SSE segment regs etc
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

    ; First arg = regs*
    mov rdi, rsp

    call isr_common_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove int_no + err_code
    add rsp, 16

    iretq

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