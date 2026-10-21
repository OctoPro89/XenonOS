[bits 64]

section .text

; void ASMCALL x64_inb(u16 port)
global x64_inb
x64_inb:
    mov dx, di      ; move lower 16 bits of RDI to DX
    in al, dx       ; read 8 bits from port into AL
    ret

; void ASMCALL x64_outb(u16 port, u8 value);
global x64_outb
x64_outb:
    mov dx, di      ; move lower 16 bits of RDI to DX
    mov al, sil     ; move lower 8 bits of RSI to AL
    out dx, al
    ret

; u16 ASMCALL x64_inw(u16 port)
global x64_inw
x64_inw:
    mov dx, di      ; move lower 16 bits of RDI to DX
    in ax, dx       ; read 16 bits from port into ax
    ret

; void ASMCALL x64_outw(u16 port, u16 value);
global x64_outw
x64_outw:
    mov dx, di      ; move lower 16 bits of RDI (port) to DX
    mov ax, si      ; move lower 16 bits of RSI (value) to AX
    out dx, ax      ; output 16-bit word to the port
    ret

; u32 ASMCALL x64_inl(u16 port);
global x64_inl
x64_inl:
    mov dx, di      ; move port from RDI (DI) to DX
    in eax, dx     ; read 32 bits from port into EAX (return register)
    ret

; void x64_outl(u16 port, u32 value)
global x64_outl
x64_outl:
    mov dx, di      ; move port from RDI (DI) to DX
    mov eax, esi    ; move 32-bit value from RSI (ESI) to EAX
    out dx, eax
    ret