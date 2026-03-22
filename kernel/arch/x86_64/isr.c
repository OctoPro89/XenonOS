#include <stdint.h>

void serial_write_str(const char*);
void serial_write_hex(uint64_t);
void serial_write_dec(uint64_t);

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;
    uint64_t err_code;
};

void isr_common_handler(struct regs* r) {
    serial_write_str("\r\n=== EXCEPTION ===\r\n");

    serial_write_str("INT: ");
    serial_write_dec(r->int_no);
    serial_write_str("\r\n");

    serial_write_str("ERR: ");
    serial_write_hex(r->err_code);
    serial_write_str(" (");
    if (r->err_code & 0x1) serial_write_str("PROT"); else serial_write_str("NOT-PRESENT");
    serial_write_str(" ");
    if (r->err_code & 0x2) serial_write_str("WRITE"); else serial_write_str("READ");
    serial_write_str(" ");
    if (r->err_code & 0x4) serial_write_str("USER"); else serial_write_str("KERNEL");
    if (r->err_code & 0x8) { serial_write_str(" "); serial_write_str("RSVD "); }
    if (r->err_code & 0x10) { serial_write_str(" "); serial_write_str("IFETCH "); }
    serial_write_str(")\r\n");

    if (r->int_no == 14) { // Page fault
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_write_str("CR2: ");
        serial_write_hex(cr2);
        serial_write_str("\r\n");
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

    serial_write_str("RIP: "); serial_write_hex(r->rax); serial_write_str("\r\n");
    serial_write_str("RFLAGS: "); serial_write_hex(r->err_code); serial_write_str("\r\n");

    while(1); // halt
}