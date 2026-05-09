[bits 64]

global msr_read
msr_read:
    rdmsr           ; Loads MSR[ECX] into EDX:EAX
    shl rdx, 32     ; Shift high bits to upper half of RDX
    or  rax, rdx    ; Combine EAX (low) and RDX (high) into RAX
    ret

global msr_write
msr_write:
    mov rax, rdx    ; Copy 64-bit value to RAX
    shr rdx, 32     ; Move high 32 bits of value into EDX
    ; EAX now contains the low 32 bits (via the mov and implicit truncation)
    wrmsr           ; Writes EDX:EAX to MSR[ECX]
    ret