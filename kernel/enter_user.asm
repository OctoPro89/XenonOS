global enter_user_mode

; extern void ASMCALL enter_user_mode(u64 entry, u64 stack); 
enter_user_mode:
    ; rdi = entry point
    ; rsi = user stack top

    cli

    mov ax, 0x23        ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x23           ; SS
    push rsi            ; RSP
    mov rax, 0x202      ; IF=1, bit 1 always set
    push rax
    push 0x1B           ; CS
    push rdi            ; RIP

    iretq