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
extern user_rsp_save

syscall_entry:
    mov [rel user_rsp_save], rsp   ; save user stack

    mov rsp, [rel kernel_stack_top]
    and rsp, -16

    push rcx
    push r11
    push rbx
    push r12
    push r13
    push r14
    push r15

    call syscall_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop r11        ; restore r11 (RFLAGS)
    pop rcx        ; restore rcx (RIP)

    mov rax, [rel user_rsp_save]

    push 0x23      ; SS (user data)
    push rax       ; RSP
    push r11       ; RFLAGS
    push 0x1B      ; CS (user code)
    push rcx       ; RIP

    iretq