; AP trampoline — copied to physical 0x8000 at runtime.
;
; All internal references use hardcoded absolute addresses based on the
; runtime location (0x8000), NOT the link-time address. These are encoded
; as immediates, not relocations, so they survive the memcpy.
;
; Startup data layout at physical 0x9000:
; +0x00 page_table_phys (uint64_t)
; +0x08 stack_top (uint64_t, higher-half VA)
; +0x10 logical_id (uint64_t)
; +0x18 c_entry (uint64_t, higher-half VA)
;
; Temp stack for 32-bit transition: grows down from 0x9FF0

global asm_ap_trampoline
global asm_ap_trampoline_end

; 16-bit real-mode entry (AP wakes here via SIPI vector 0x8)
bits 16
asm_ap_trampoline:
    cli
    cld
    ; far jump to 0x0000:0x8080 to clear CS cache and ensure CS = 0
    jmp 0x0000:0x8080

align 16
_ap_gdt_table:                  ; 0x8010
    dq 0x0000000000000000    ; 0x00: null
    dq 0x00CF9A000000FFFF    ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF    ; 0x10: 32-bit data, B=1
    dq 0x00CF890000000068    ; 0x18: TSS placeholder
    dq 0x00209A000000FFFF    ; 0x20: 64-bit code
    dq 0x00CF92000000FFFF    ; 0x28: data descriptor

; GDT pointer at 0x8040
_ap_gdt_ptr:                    ; 0x8040
    dw _ap_gdt_ptr - _ap_gdt_table - 1 ; limit = 47 = 0x2F
    dd 0x8010                   ; base (runtime physical address)

align 64
; 16-bit: set up segments and enter protected mode
_ap_real_mode:                  ; 0x8080
    xor ax, ax
    mov ds, ax
    
    lgdt [0x8040]
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; NASM far jump to 32-bit protected mode descriptor 0x08
    jmp 0x08:0x80A0

; 32-bit protected mode: set CR3/CR0/CR4/EFER, enable paging
align 32
bits 32
_ap_prot_mode:                  ; 0x80A0
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov esp, 0x9FF0
    mov ebp, esp

    ; load page table root from startup data (low 32 bits, assumed < 4GB)
    mov eax, dword [0x9000]
    mov cr3, eax

    ; CR0: clear CD+NW, set WP+NE+MP+PE+ET
    mov eax, cr0
    and eax, ~((1 << 30) | (1 << 29))
    or eax, (1 << 16) | (1 << 5) | (1 << 4) | (1 << 1) | (1 << 0)
    mov cr0, eax

    ; CR4: PAE + PGE + OSFXSR + OSXMMEXCPT + FSGSBASE
    ; mov eax, cr4
    ; or eax, (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10) | (1 << 16)
    ; mov cr4, eax
    mov eax, cr4
    ; Enable PAE, PGE, OSFXSR, and OSXMMEXCPT.
    ; Do not set CR4.FSGSBASE (bit 16) yet.
    or eax, (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; EFER: SCE + LME + NXE
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 0) | (1 << 8) | (1 << 11)
    wrmsr

    ; enable paging
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; Fixed Long Mode far jump using NASM-compatible push-and-retf technique
    push 0x20                   ; 64-bit code segment selector
    push 0x8100                 ; target offset address
    retf                        ; performs the transition to 64-bit

; 64-bit long mode: load stack, args, call C entry
align 64
bits 64
_ap_long_mode:                  ; 0x8100
    ; using explicit absolute non-RIP-relative pointers to guarantee survival over memcpy
    mov rbx, 0x9000
    mov rsp, [rbx + 0x08]       ; stack_top (higher-half VA)
    mov rbp, rsp
    mov rdi, [rbx + 0x10]       ; logical_id (first C arg)
    mov rax, [rbx + 0x18]       ; c_entry (higher-half VA)
    
    call rax                    ; should never return

cli
_ap_halt:
    hlt
    jmp _ap_halt

asm_ap_trampoline_end: