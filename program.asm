[bits 64]
mov rax, 1
lea rdi, [rel hello_string]
mov rsi, 12
syscall

mov rax, 1
lea rdi, [rel hi_string]
mov rsi, 3
syscall

jmp $

section .data
    hello_string db 'Hello World', 0x0A
    hi_string db 'Hi', 0x0A