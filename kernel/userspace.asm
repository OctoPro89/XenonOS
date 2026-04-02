[bits 64]

global to_userspace
to_userspace:
    mov rcx, rdi
    mov rsp, rsi
    mov r11, 0x0202
    sysretq
