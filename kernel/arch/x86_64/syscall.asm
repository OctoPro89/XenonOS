global syscall_init

extern syscall_handler
extern current_task

; SYSCALL setup
syscall_init:
    ; Enable SYSCALL (EFER.SCE)
    mov rcx, 0xC0000080
    rdmsr
    or eax, 1
    wrmsr

    ; STAR
    ; kernel CS = 0x08
    ; user CS   = 0x18
    mov rcx, 0xC0000081

    mov rax, (0x08 << 32) | (0x18 << 48)
    mov rdx, rax
    shr rdx, 32

    wrmsr

    ; LSTAR
    mov rcx, 0xC0000082
    mov rax, syscall_entry
    mov rdx, rax
    shr rdx, 32

    wrmsr

    ; FMASK
    mov rcx, 0xC0000084
    mov eax, (1 << 9)
    xor edx, edx
    wrmsr

    ret

; syscall_entry
;
; Important:
;   Every task gets its own kernel stack
;   User RSP/RIP/RFLAGS are stored in task_t
syscall_entry:
    ; r10 is caller-saved, so we can use it
    ; as our current-task pointer.
    mov r10, [rel current_task]

    ; save userspace state into the current task
    mov [r10 + 0x00], rsp
    mov [r10 + 0x08], rcx
    mov [r10 + 0x10], r11

    ; switch to this task's kernel stack
    mov rsp, [r10 + 0x18]
    and rsp, -16

    ; save registers from syscall_regs plus current task stuff
    ; NOTE: r10 is intentionally a syscall-clobbered register for now and contains the current_task
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rbx
    push rax

    mov rdi, rsp
    call syscall_handler

    mov rax, [rsp]

    ; Discard saved RAX.
    add rsp, 8

    ; Restore everything else.
    pop rbx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; r10 is caller-saved, so it doesn't matter that it no longer contains the user's original value
    ; NOTE: must update syscalls in C if this is changed later
    mov r10, [rel current_task]

    mov rcx, [r10 + 0x08]
    mov r11, [r10 + 0x10]
    push 0x23
    push qword [r10 + 0x00]
    push r11
    push 0x1B
    push rcx

    iretq