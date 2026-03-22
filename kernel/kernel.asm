section .text

extern kernel_main_trampoline
global kernel_start

kernel_start:
    ; send 0x47 to COM1
    ; mov al, 0x47
    ; mov dx, 0x3F8
    ; out dx, al

    ; BootInfo* argument is in RDI from the bootloader, pass it along to trampoline
    call kernel_main_trampoline

    ; This should never happen
    cli
    hlt