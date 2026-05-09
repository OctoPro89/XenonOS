[bits 64]

global cpuid_read_vendor_id
cpuid_read_vendor_id:
    push rbx            ; RBX is non-volatile; must be saved
    
    xor eax, eax        ; EAX = 0 (CPUID_VENDOR_ID leaf)
    cpuid               ; Returns vendor string in EBX, EDX, ECX
    
    ; The vendor string is 12 bytes long, returned in order: EBX, EDX, ECX
    mov [rcx], ebx      ; vendor[0-3]
    mov [rcx + 4], edx  ; vendor[4-7]
    mov [rcx + 8], ecx  ; vendor[8-11]
    mov byte [rcx + 12], 0 ; Null terminator
    
    pop rbx             ; Restore RBX
    ret

global cpuid_read_cpu_family
cpuid_read_cpu_family:
    push rbx                ; Save RBX (callee-saved in x64 ABI)

    mov eax, 1              ; CPUID leaf 1: Processor Info and Feature Bits
    cpuid                   ; Result in EAX, EBX, ECX, EDX

    ; Extract Base Family: (eax >> 8) & 0xF
    mov ecx, eax
    shr ecx, 8
    and ecx, 0Fh            ; ECX = base_family

    ; Extract Extended Family: (eax >> 20) & 0xFF
    mov edx, eax
    shr edx, 20
    and edx, 0FFh           ; EDX = extended_family

    ; Logic: family = (base_family == 0xF) ? (base_family + extended_family) : base_family
    cmp ecx, 0Fh
    jne .use_base           ; If base_family != 0xF, skip addition
    add ecx, edx            ; family = base_family + extended_family

.use_base:
    mov eax, ecx            ; Move final family value to return register

    pop rbx                 ; Restore RBX
    ret