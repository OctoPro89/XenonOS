global syscall_init
extern syscall_handler
extern kernel_stack_top

; --------------------------------------------
; Initialize SYSCALL/SYSRET
; --------------------------------------------
syscall_init:
    ; --- Enable SYSCALL (EFER.SCE = bit 0) ---
    mov rcx, 0xC0000080
    rdmsr
    or eax, 1
    wrmsr

    ; --- STAR MSR (kernel/user CS selectors) ---
    ; kernel CS = 0x08, user CS = 0x18
    mov rcx, 0xC0000081

    mov rax, (0x08 << 32) | (0x18 << 48)
    mov rdx, rax
    shr rdx, 32

    wrmsr

    ; --- LSTAR MSR (syscall entry point) ---
    mov rcx, 0xC0000082
    mov rax, syscall_entry
    mov rdx, rax
    shr rdx, 32
    wrmsr

    ; --- FMASK MSR (disable IF on syscall) ---
    mov rcx, 0xC0000084
    mov eax, (1 << 9)      ; clear IF
    xor edx, edx
    wrmsr

    ret

; --------------------------------------------
; Syscall entry point (called via SYSCALL)
; Kernel stack is switched here
; --------------------------------------------
extern user_rsp_save

syscall_entry:
    ; save critical values
    mov r12, rcx    ; user RIP
    mov r13, r11    ; user RFLAGS

    mov [rel user_rsp_save], rsp

    mov rsp, [rel kernel_stack_top]
    and rsp, -16

    ; save all regs except rcx/r11 (already saved them)
    push r15
    push r14
    push r13   ; saved r11
    push r12   ; saved rcx
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

    ; Restore
    pop rax
    pop rbx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r12   ; rcx
    pop r13   ; r11
    pop r14
    pop r15

    ; restore syscall state
    mov rcx, r12
    mov r11, r13

    mov rax, [rel user_rsp_save]

    push 0x23
    push rax
    push r11
    push 0x1B
    push rcx

    iretq