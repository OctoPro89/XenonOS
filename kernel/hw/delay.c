#include <hw/delay.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/sync/critical_section.h>

const u32 PIT_FREQ     = 1193182;
const u16 PIT_CTRL     = 0x43;
const u16 PIT_CH2_DATA = 0x42;
const u16 PORT_B       = 0x61;
const u8  PORT_B_GATE  = 0x01;
const u8  PORT_B_SPKR  = 0x02;
const u8  PORT_B_OUT2  = 0x20;
const u32 MAX_CHUNK_MS = 50;

__PRIVILEGED_CODE void delay_pit_ms(u32 ms) {
    if (ms == 0) { return; }

    while (ms > 0) {
        u32 chunk = (ms > MAX_CHUNK_MS) ? MAX_CHUNK_MS : ms;
        u16 count = (u16)((PIT_FREQ * chunk) / 1000);
        if (count == 0) count = 1;

        u64 flags = irq_save();

        u8 port_b_orig = x64_inb(PORT_B);

        x64_outb(PORT_B, (port_b_orig & ~PORT_B_SPKR) & ~PORT_B_GATE);

        x64_outb(PIT_CTRL, 0xB0);
        x64_outb(PIT_CH2_DATA, (u8)(count & 0xFF));
        x64_outb(PIT_CH2_DATA, (u8)(count >> 8));

        x64_outb(PORT_B, (port_b_orig & ~PORT_B_SPKR) | PORT_B_GATE);

        while ((x64_inb(PORT_B) & PORT_B_OUT2) == 0) {
            asm volatile("pause");
        }

        x64_outb(PORT_B, port_b_orig);

        irq_restore(flags);

        ms -= chunk;
    }
}