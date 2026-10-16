[bits 64]

global cpuid_read_vendor_id
cpuid_read_vendor_id:
    push rbx            ; RBX is non-volatile; must be saved
    
    xor eax, eax        ; EAX = 0 (CPUID_VENDOR_ID leaf)
    cpuid               ; Returns vendor string in EBX, EDX, ECX
    
    ; System V: The first argument (char* vendor) is in RDI
    mov [rdi], ebx      ; vendor[0-3]
    mov [rdi + 4], edx  ; vendor[4-7]
    mov [rdi + 8], ecx  ; vendor[8-11]
    mov byte [rdi + 12], 0 ; Null terminator
    
    pop rbx             ; Restore RBX
    ret

global cpuid_read_cpu_family
cpuid_read_cpu_family:
    push rbx                ; Save RBX (callee-saved)

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

    cmp ecx, 0Fh
    jne .use_base           
    add ecx, edx            ; family = base_family + extended_family

.use_base:
    mov eax, ecx            ; Move final family value to return register (EAX)

    pop rbx                 ; Restore RBX
    ret

global cpuid_raw
; void cpuid_raw(uint32_t leaf, uint32_t subleaf, uint32_t* regs);
; Inputs: RDI = leaf, RSI = subleaf, RDX = regs pointer
cpuid_raw:
    push rbx

    mov r8, rdx             ; Preserve output pointer
    mov eax, edi            ; EAX = leaf
    mov ecx, esi            ; ECX = subleaf

    cpuid

    mov [r8],      eax      ; regs[0]
    mov [r8 + 4],  ebx      ; regs[1]
    mov [r8 + 8],  ecx      ; regs[2]
    mov [r8 + 12], edx      ; regs[3]

    pop rbx
    ret