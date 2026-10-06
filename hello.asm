BITS 64
DEFAULT REL

%define SYS_WRITE 1
%define SYS_EXIT  3

%define STDOUT    1

SECTION .text

GLOBAL _start

_start:
    ; write(STDOUT, message, message_len)

    mov eax, SYS_WRITE
    mov edi, STDOUT

    lea rsi, [rel message]

    mov edx, message_len

    syscall


    ; exit(0)

    mov eax, SYS_EXIT
    xor edi, edi

    syscall


.hang:
    jmp .hang


SECTION .rodata

message:
    db "Hello from /bin/hello!", 10

message_len equ $ - message