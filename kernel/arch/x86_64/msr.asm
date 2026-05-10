[bits 64]

global msr_read
msr_read:
    mov ecx, edi        ; MSR index (SysV → ECX)
    rdmsr               ; EDX:EAX = result

    shl rdx, 32
    or  rax, rdx
    ret

global msr_write
msr_write:
    mov ecx, edi        ; MSR index

    mov rax, rsi        ; 64-bit value

    mov rdx, rax
    shr rdx, 32         ; high 32 bits
    mov eax, eax        ; low 32 bits already in eax

    wrmsr
    ret