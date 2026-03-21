global kernel_start
section .text
kernel_start:
    ; send 0x47 to COM1
    mov al, 0x47
    mov dx, 0x3F8
    out dx, al

    cli
.halt:
    hlt
    jmp .halt