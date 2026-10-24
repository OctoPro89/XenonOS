default rel

section .text

; x86_64_gdt_flush(const GDTPtr* gdtr, u16 cs, u16 ds, u16 tss)
;
; parameters (System V AMD64 ABI):
;   rdi = pointer to gdt_ptr structure
;   rsi = kernel code segment selector (CS)
;   rdx = kernel data segment selector (DS)
;   rcx = TSS selector
global x86_64_gdt_flush
x86_64_gdt_flush:
    ; load the new GDT (x86_64 GDTR descriptor size requires OWORD/10-byte pointer)
    lgdt [rdi]

    ; save parameters we'll need after segment reload
    mov rax, rsi        ; CS selector
    mov r8, rdx         ; DS selector
    mov r9, rcx         ; TSS selector

    ; reload CS via far return.
    lea r10, [reload_cs]
    push rax            ; push new CS
    push r10            ; push return address
    retfq               ; far return: pops RIP and CS

reload_cs:
    ; Now running with new CS. Reload data segment registers.
    mov ax, r8w
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Zero FS selector.
    xor eax, eax
    mov fs, ax

    ; save IA32_GS_BASE, zero GS selector, then restore IA32_GS_BASE.
    ; this preserves the per-CPU base set by percpu::init_bsp().
    mov ecx, 0xC0000101     ; IA32_GS_BASE MSR
    rdmsr                   ; EDX:EAX = current GS base
    mov r10d, eax           ; save low 32 bits
    mov r11d, edx           ; save high 32 bits

    xor eax, eax
    mov gs, ax              ; zero GS selector (clears hidden base)

    mov eax, r10d           ; restore low 32 bits
    mov edx, r11d           ; restore high 32 bits
    mov ecx, 0xC0000101     ; IA32_GS_BASE MSR
    wrmsr                   ; restore GS base

    ; Load the Task State Segment.
    ltr r9w

    ret