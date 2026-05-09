[bits 64]

global tlb_flush_all
tlb_flush_all:
    mov rax, cr3
    mov cr3, rax