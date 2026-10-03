[bits 64]

global x86_64_restore_interrupt_context

x86_64_restore_interrupt_context:
    ; RDI = task->rsp

    mov rsp, rdi

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

    add rsp, 16        ; int_no + err_code

    iretq