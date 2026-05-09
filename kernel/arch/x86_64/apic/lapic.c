#include <arch/x86_64/apic/lapic.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/io.h>
#include <memory/paging.h>
#include <memory/vmm.h>

// ICR Register Offsets in the LAPIC MMIO space
#define APIC_REG_ICR_LOW          0x300 // Interrupt Command Register [31:0]
#define APIC_REG_ICR_HIGH         0x310 // Interrupt Command Register [63:32]

// Bits in ICR_LOW (32 bits)
#define APIC_VECTOR_MASK          0x000000FF  // Bits [7:0]:  Interrupt vector
// Delivery Mode (bits [10:8]) 
#define APIC_DM_FIXED             (0 << 8)    // 000: Fixed
#define APIC_DM_LOWEST            (1 << 8)    // 001: Lowest Priority
#define APIC_DM_SMI               (2 << 8)    // 010: SMI (system management interrupt)
#define APIC_DM_NMI               (4 << 8)    // 100: NMI
#define APIC_DM_INIT              (5 << 8)    // 101: INIT
#define APIC_DM_STARTUP           (6 << 8)    // 110: STARTUP
// Destination Mode (bit 11)
#define APIC_DESTMODE_LOGICAL     (1 << 11)   // 1 for logical, 0 for physical
// Delivery Status (bit 12)
#define APIC_DELIVERY_STATUS      (1 << 12)   // 1 if APIC is busy sending
// Level (bit 14) - used only for INIT level de-assert
#define APIC_LEVEL_ASSERT         (1 << 14)  
#define APIC_LEVEL_DEASSERT       (0 << 14) // same bit = 0
// Trigger Mode (bit 15)
#define APIC_TRIGGER_LEVEL        (1 << 15)   // 1 for level, 0 for edge
#define APIC_TRIGGER_EDGE         (0 << 15)   
// Destination Shorthand (bits [19:18])
//  00 = No shorthand, 01 = Self, 02 = All including self, 03 = All excluding self
// Usually we leave it at 00 because we specify the APIC ID in ICR_HIGH.

// Bits in ICR_HIGH (32 bits)
#define APIC_ICR_DEST_SHIFT       24          // Bits [31:24]: Destination APIC ID

__PRIVILEGED_DATA static paddr_t lapic_physical_base = 0;
__PRIVILEGED_DATA static volatile u32* lapic_virtual_base = 0;

static void _lapic_init(paddr_t base, u8 spurious_irq) {
    if (!lapic_physical_base) {
        vaddr_t virt_base = vmm_map_physical_page(&kernel_space, base, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE); // TODO: not sure about these paging flags
        lapic_virtual_base = (volatile u32*)virt_base;
    }

    // set the spurious interrupt vector
    u32 spurious_vector = lapic_read(0xF0);
    spurious_vector |= (1 << 8); // enable the APIC
    spurious_vector |= spurious_irq; // set the spurious interrupt vector
    lapic_write(0xF0, spurious_vector);

    // disable legacy pic controller
    lapic_disable_legacy_pic();
}

__PRIVILEGED_CODE void lapic_init() {
    u64 apic_base_msr = msr_read(IA32_APIC_BASE_MSR);

    // enable apic by setting 11th bit
    apic_base_msr |= (1 << 11);

    msr_write(IA32_APIC_BASE_MSR, apic_base_msr);
    paddr_t physical_base = (paddr_t)apic_base_msr & ~0xFFF;
    _lapic_init(physical_base, 0xFF);
}

__PRIVILEGED_CODE u32 lapic_read(u32 reg) {
    return lapic_virtual_base[reg / 4];
}

__PRIVILEGED_CODE void lapic_write(u32 reg, u32 value) {
    lapic_virtual_base[reg / 4] = value;
}

__PRIVILEGED_CODE void lapic_mask_irq(u32 lvtoff) {
    // read the current LVT entry
    u32 lvt_entry = lapic_read(lvtoff);

    // set the mask bit (bit 16)
    lvt_entry |= (1 << 16);

    // write the modified LVT entry back
    lapic_write(lvtoff, lvt_entry);
}

__PRIVILEGED_CODE void lapic_unmask_irq(u32 lvtoff) {
    // read the current LVT entry
    u32 lvt_entry = lapic_read(lvtoff);

    // clear the mask bit (bit 16)
    lvt_entry &= ~(1 << 16);

    // write the modified LVT entry back
    lapic_write(lvtoff, lvt_entry);
}

__PRIVILEGED_CODE void lapic_mask_timer_irq() {
    lapic_mask_irq(APIC_LVT_TIMER);
}

__PRIVILEGED_CODE void lapic_unmask_timer_irq() {
    lapic_unmask_irq(APIC_LVT_TIMER);
}

__PRIVILEGED_CODE void lapic_complete_irq() {
    lapic_write(0xB0, 0x00);
}

__PRIVILEGED_CODE void lapic_send_init_ipi(u8 apic_id) {
 // ------------------------------------------------------
    // Write to ICR_HIGH: set the target APIC ID
    // ------------------------------------------------------
    //
    // Bits [31:24] = Destination APIC ID
    // The lower 24 bits of ICR_HIGH are reserved (must be 0).
    // 
    lapic_write(APIC_REG_ICR_HIGH, (u32)(apic_id) << APIC_ICR_DEST_SHIFT);

    // ------------------------------------------------------
    // Write to ICR_LOW: set the command for INIT IPI
    // ------------------------------------------------------
    //
    // For an INIT IPI:
    //   - Vector = 0 (bits [7:0] = 0)
    //   - Delivery Mode = 101b (INIT)
    //   - Level = 1 (Assert) 
    //   - Trigger Mode = 1 (Level)
    //   - Destination Mode = 0 (Physical, if you want physical APIC ID)
    //   - Destination Shorthand = 00 (bits [19:18] = 0)
    //
    // Putting it all together:
    //   ICR_LOW = APIC_DM_INIT | APIC_TRIGGER_LEVEL | APIC_LEVEL_ASSERT
    //
    u32 icr_low = APIC_DM_INIT | APIC_TRIGGER_LEVEL | APIC_LEVEL_ASSERT;
    lapic_write(APIC_REG_ICR_LOW, icr_low);

    // ------------------------------------------------------
    // Wait for the send to complete
    // ------------------------------------------------------
    lapic_wait_for_icr_cmd_completion();

    // ------------------------------------------------------
    // De-assert the INIT IPI (per Intel specs),
    // done by writing the same command but with
    // Level=0 (deassert).
    // ------------------------------------------------------
    lapic_write(APIC_REG_ICR_HIGH, (u32)apic_id << APIC_ICR_DEST_SHIFT);

    // APIC_DM_INIT | APIC_TRIGGER_LEVEL is still set, but now Level=0
    icr_low = APIC_DM_INIT | APIC_TRIGGER_LEVEL | APIC_LEVEL_DEASSERT;
    lapic_write(APIC_REG_ICR_LOW, icr_low);

    // Wait again for send to complete
    lapic_wait_for_icr_cmd_completion();
}

__PRIVILEGED_CODE void lapic_send_startup_ipi(u8 apic_id, u32 vector) {
// ------------------------------------------------------
    // Write ICR_HIGH: set the target APIC ID
    // ------------------------------------------------------
    lapic_write(APIC_REG_ICR_HIGH, (u32)(apic_id) << APIC_ICR_DEST_SHIFT);

    // ------------------------------------------------------
    // Write ICR_LOW: set command for STARTUP IPI
    // ------------------------------------------------------
    //
    // For a STARTUP IPI:
    //   - Vector = startup_vector (bits [7:0])
    //   - Delivery Mode = 110b (STARTUP)
    //   - Trigger Mode = 0 (Edge)
    //   - Level = 1 (Assert) — but for STARTUP, we treat it as edge,
    //       so bit 15 = 0 for edge, bit 14 can be 0 or 1. Typically 0 is used 
    //       in practice (Intel’s examples vary but edge triggers do not rely on level).
    //
    // The CPU will start execution at physical address = startup_vector * 0x1000.
    //
    // So:
    //    ICR_LOW = (startup_vector & APIC_VECTOR_MASK)
    //               | APIC_DM_STARTUP
    //               | APIC_TRIGGER_EDGE
    //               | ...
    //
    u32 icr_low = ((u32)(vector) & APIC_VECTOR_MASK) | APIC_DM_STARTUP | APIC_TRIGGER_EDGE;

    lapic_write(APIC_REG_ICR_LOW, icr_low);

    // wait for completion
    lapic_wait_for_icr_cmd_completion();
}

__PRIVILEGED_CODE void lapic_wait_for_icr_cmd_completion() {
    // wait until the Delivery Status bit is cleared, meaning the IPI has been sent (the hardware is not busy anymore)
    while (lapic_virtual_base[APIC_REG_ICR_LOW / 4] & APIC_DELIVERY_STATUS) {
        asm volatile("pause");
    }
}

__PRIVILEGED_CODE void lapic_disable_legacy_pic() {
    // send the disable command (0xFF) to both PIC1 and PIC2 data ports
    x64_outb(0xA1, 0xFF);
    x64_outb(0x21, 0xFF);
}