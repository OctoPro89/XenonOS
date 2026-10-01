#include <xlibc/xstdint.h>
#include <xlibc/stdio.h> // TODO: remove
#include <graphics/graphics.h> // TODO: remove
#include <arch/x86_64/irq.h>

void serial_write_str(const char*);
void serial_write_hex(uint64_t);
void serial_write_dec(uint64_t);

static void exception_panic(struct regs* r) {
    static const char* exception_names[] = {
        "Divide-by-zero Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
        "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
        "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved"
    };

    // while(1);

    serial_write_str("\r\n=== EXCEPTION: ");
    if (r->int_no < 32) {
        serial_write_str(exception_names[r->int_no]);
    } else {
        serial_write_str("User Defined Interrupt");
    }
    serial_write_str(" ===\r\n");

    serial_write_str("INT: ");  serial_write_dec(r->int_no);
    serial_write_str("  ERR: "); serial_write_hex(r->err_code);
    serial_write_str("\r\n");

    serial_write_str("RIP:    ");
    serial_write_hex(r->rip);
    serial_write_str("  CS:  ");
    serial_write_hex(r->cs);
    serial_write_str("\r\n");

    serial_write_str("RFLAGS: ");
    serial_write_hex(r->rflags);
    serial_write_str("\r\n");

    // TODO: I think this is correct
    if ((r->cs & 3) == 3) {
        serial_write_str("USP:    ");
        serial_write_hex(r->user_rsp);
        serial_write_str("  SS:  ");
        serial_write_hex(r->ss);
        serial_write_str("\r\n");
    }

    // Specific decoding for Page Fault (INT 14)
    if (r->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_write_str("Faulting Address (CR2): "); serial_write_hex(cr2); serial_write_str("\r\n");
        
        serial_write_str("Cause: [");
        if (r->err_code & 0x01) serial_write_str("P "); else serial_write_str("NP "); // Present
        if (r->err_code & 0x02) serial_write_str("WR "); else serial_write_str("RD "); // Write
        if (r->err_code & 0x04) serial_write_str("US "); else serial_write_str("KS "); // User/Supervisor
        if (r->err_code & 0x10) serial_write_str("ID ");                             // Instruction Fetch
        serial_write_str("]\r\n");
    }

    // Print general-purpose registers
    serial_write_str("\r\nRegisters:\r\n");
    serial_write_str("RAX: "); serial_write_hex(r->rax); serial_write_str("\r\n");
    serial_write_str("RBX: "); serial_write_hex(r->rbx); serial_write_str("\r\n");
    serial_write_str("RCX: "); serial_write_hex(r->rcx); serial_write_str("\r\n");
    serial_write_str("RDX: "); serial_write_hex(r->rdx); serial_write_str("\r\n");
    serial_write_str("RSI: "); serial_write_hex(r->rsi); serial_write_str("\r\n");
    serial_write_str("RDI: "); serial_write_hex(r->rdi); serial_write_str("\r\n");
    serial_write_str("RBP: "); serial_write_hex(r->rbp); serial_write_str("\r\n");
    serial_write_str("R8 : "); serial_write_hex(r->r8 ); serial_write_str("\r\n");
    serial_write_str("R9 : "); serial_write_hex(r->r9 ); serial_write_str("\r\n");
    serial_write_str("R10: "); serial_write_hex(r->r10); serial_write_str("\r\n");
    serial_write_str("R11: "); serial_write_hex(r->r11); serial_write_str("\r\n");
    serial_write_str("R12: "); serial_write_hex(r->r12); serial_write_str("\r\n");
    serial_write_str("R13: "); serial_write_hex(r->r13); serial_write_str("\r\n");
    serial_write_str("R14: "); serial_write_hex(r->r14); serial_write_str("\r\n");
    serial_write_str("R15: "); serial_write_hex(r->r15); serial_write_str("\r\n");

    // Attempt graphical display
    printf("\r\n=== EXCEPTION: ");
    if (r->int_no < 32) {
        printf("%s", exception_names[r->int_no]);
    } else {
        printf("User Defined Interrupt");
    }
    printf(" ===\r\n");

    printf("INT:    ");  serial_write_dec(r->int_no);
    printf("  ERR: "); printf("%x", r->err_code);
    printf("\r\n");

    printf("RIP:    "); printf("%x", r->rip);
    printf("  CS:  ");   printf("%x", r->cs);     printf("\r\n");
    printf("RFLAGS: "); printf("%x", r->rflags);
    printf("  USP: ");  printf("%x", r->user_rsp); printf("\r\n");
    printf("SS:     "); printf("%x", r->ss);      printf("\r\n");

    // Specific decoding for Page Fault (INT 14)
    if (r->int_no == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        printf("Faulting Address (CR2): "); printf("%x", cr2); printf("\r\n");
        
        printf("Cause: [");
        if (r->err_code & 0x01) printf("P ");  else printf("NP "); // Present
        if (r->err_code & 0x02) printf("WR "); else printf("RD "); // Write
        if (r->err_code & 0x04) printf("US "); else printf("KS "); // User/Supervisor
        if (r->err_code & 0x10) printf("ID ");                     // Instruction Fetch
        printf("]\r\n");
    }

    // Print general-purpose registers
    printf("\r\nRegisters:\r\n");
    printf("RAX: "); printf("%x", r->rax); printf("\r\n");
    printf("RBX: "); printf("%x", r->rbx); printf("\r\n");
    printf("RCX: "); printf("%x", r->rcx); printf("\r\n");
    printf("RDX: "); printf("%x", r->rdx); printf("\r\n");
    printf("RSI: "); printf("%x", r->rsi); printf("\r\n");
    printf("RDI: "); printf("%x", r->rdi); printf("\r\n");
    printf("RBP: "); printf("%x", r->rbp); printf("\r\n");
    printf("R8 : "); printf("%x", r->r8 ); printf("\r\n");
    printf("R9 : "); printf("%x", r->r9 ); printf("\r\n");
    printf("R10: "); printf("%x", r->r10); printf("\r\n");
    printf("R11: "); printf("%x", r->r11); printf("\r\n");
    printf("R12: "); printf("%x", r->r12); printf("\r\n");
    printf("R13: "); printf("%x", r->r13); printf("\r\n");
    printf("R14: "); printf("%x", r->r14); printf("\r\n");
    printf("R15: "); printf("%x", r->r15); printf("\r\n");
    

    while(1); // halt
}

void isr_common_handler(struct regs* r) {
    if (r->int_no < 32) {
        exception_panic(r);
        return;
    }

    irq_dispatch(r);
}