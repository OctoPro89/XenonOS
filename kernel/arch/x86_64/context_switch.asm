[bits 64]

global x86_64_context_switch

; extern ASMCALL void x86_64_context_switch(u64* old_rsp, u64 new_rsp);
; SysV:
; RDI = address where current RSP is saved
; RSI = new RSP
;
; Preserve SysV callee-saved registers: RBX, RBP, R12-R15
;
; The stack layout after the pushes is:
;
;   [rsp +  0] = r15
;   [rsp +  8] = r14
;   [rsp + 16] = r13
;   [rsp + 24] = r12
;   [rsp + 32] = rbp
;   [rsp + 40] = rbx
;   [rsp + 48] = return address
x86_64_context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Save current task's stack pointer
    mov [rdi], rsp

    ; Switch stacks
    mov rsp, rsi

    ; Restore next task's callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Resume the next task
    ret