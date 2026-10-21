#include <power/power.h>
#include <arch/x86_64/power_s5.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/idt.h>
#include <acpi/acpi.h>
#include <acpi/fadt.h>
#include <memory/paging.h>
#include <memory/vmm.h>
#include <hw/mmio.h>
#include <time/time.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>

// resolved PM1 control register, either an I/O port or mapped MMIO
typedef struct {
    b8 valid;
    b8 is_io;
    u16 port;
    vaddr_t va;
} pm1_block_t;

static const u16 KBC_CMD_PORT           = 0x64;
static const u8 KBC_STATUS_IBF          = 0x02;
static const u8 KBC_CMD_PULSE_RESET     = 0xFE;
static const u32 KBC_DRAIN_ATTEMPTS     = 500;

static const u16 RESET_CTRL_PORT        = 0xCF9;
static const u8 RESET_CTRL_ARM          = 0x02;  // Select CPU reset without power cycle
static const u8 RESET_CTRL_FIRE         = 0x06;  // RST_CPU + SYS_RST, triggers the reset

static const u64 MECHANISM_WAIT_US      = 50000;

// PM1 control register bits
static const u16 PM1_SCI_EN             = (1 << 0);
static const u16 PM1_SLP_EN             = (1 << 13);
static const u16 PM1_SLP_TYP_MASK       = (0x7 << 10);
static const u32 PM1_SLP_TYP_SHIFT      = 10;

static const u32 SCI_EN_POLL_ATTEMPTS = 10000;
static const u64 SCI_EN_POLL_DELAY_US = 100;

/**
 * @brief Reser through the FADT reset register, the mechanism the firmware declares for the board.
 * @note Absent or unusable registers are skipped.
 */
__PRIVILEGED_CODE static void acpi_reset() {
    if (!FADT_HAS_FIELD(&fadt_data, reset_value)) {
        serial_write_str("FADT doesn't support reset_value!");
        return;
    }

    if (!(fadt_data.flags & FADT_FLAG_RESET_REG_SUP) || fadt_data.reset_reg.address == 0) {
        serial_write_str("FADT doesn't support reset_value!");
        return;
    }

    switch (fadt_data.reset_reg.address_space) {
        case ACPI_GENERIC_ADDRESS_STRUCTURE_SPACE_SYSTEM_IO: {
            x64_outb((u16)(fadt_data.reset_reg.address), fadt_data.reset_value);
            break;
        }
        case ACPI_GENERIC_ADDRESS_STRUCTURE_SPACE_SYSTEM_MEMORY: {
            vaddr_t va = vmm_map_physical_page(&kernel_space, (paddr_t)fadt_data.reset_reg.address, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH);
            if (!va) {
                serial_write_str("VMM failed to map FADT!");
                return;
            }

            mmio_write8(va, fadt_data.reset_value);
            break;
        }
        default: {
            serial_write_str("Unhandled FADT register space!");
            break;
        }
    }
}

/**
 * @brief Pulse the CPU reset line through the keyboard controller
 */
__PRIVILEGED_CODE static void kbc_reset() {
    for (u32 i = 0; i < KBC_DRAIN_ATTEMPTS; ++i) {
        if ((x64_inb(KBC_CMD_PORT) & KBC_STATUS_IBF) == 0) {
            break;
        }

        usleep(10);
    }

    x64_outb(KBC_CMD_PORT, KBC_CMD_PULSE_RESET);
}

/**
 * @brief Load an empty IDT and trap, the fault cannot be delivered which escalates a triple fault and resets the CPU.
 */
__PRIVILEGED_CODE static void triple_fault() {
    struct IDTR null_idt = { 0, 0 };
    asm volatile("lidt %0; int3" :: "m"(null_idt));
}


/**
 * Map the DSDT through the FADT pointer and extract the \_S5 values.
 */
__PRIVILEGED_CODE static b8 lookup_s5_sleep_types(const fadt_table_t* fadt, u8* typ_a, u8* typ_b) {
    u64 dsdt_phys = fadt->dsdt;
    if (FADT_HAS_FIELD(fadt, x_dsdt) && fadt->x_dsdt != 0) {
        dsdt_phys = fadt->x_dsdt;
    }

    const acpi_sdt_header_t* dsdt = acpi_map_table(dsdt_phys);
    if (!dsdt || memcmp(dsdt->signature, "DSDT", 4) != 0) {
        return false;
    }

    const u8* aml = (const uint8_t*)(dsdt) + sizeof(acpi_sdt_header_t);
    size_t aml_len = dsdt->length - sizeof(acpi_sdt_header_t);

    return find_s5_sleep_types(aml, aml_len, typ_a, typ_b);
}

/**
 * @brief Resolve a PM1 control block, preferring the extended GAS form over the legacy port field when FADT provides a usable one.
 */
__PRIVILEGED_CODE static pm1_block_t resolve_pm1_cnt(const generic_address_structure_t* xreg, u32 legacy_port) {
    pm1_block_t blk = {};

    if (xreg && xreg->address != 0) {
        if (xreg->address_space == ACPI_GENERIC_ADDRESS_STRUCTURE_SPACE_SYSTEM_IO) {
            blk.valid = true;
            blk.is_io = true;
            blk.port = (u16)xreg->address;
            return blk;
        }

        if (xreg->address_space == ACPI_GENERIC_ADDRESS_STRUCTURE_SPACE_SYSTEM_MEMORY) {
            vaddr_t va = vmm_map_physical_page(&kernel_space, (paddr_t)xreg->address, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH);
            if (va) {
                blk.valid = true;
                blk.va = va;
                return blk;
            }
        }
    }

    if (legacy_port != 0) {
        blk.valid = true;
        blk.is_io = true;
        blk.port = (u16)legacy_port;
    }

    return blk;
}

__PRIVILEGED_CODE static __hint_inline__ u16 pm1_read(const pm1_block_t* blk) {
    return blk->is_io ? x64_inw(blk->port) : mmio_read16(blk->va);
}

__PRIVILEGED_CODE static __hint_inline__ void pm1_write(const pm1_block_t* blk, u16 val) {
    if (blk->is_io) {
        x64_outw(blk->port, val);
    }
    else {
        mmio_write16(blk->va, val);
    }
}

/**
 * @brief Put the chipset into ACPI mode if firmware left it in legacy mode. The PM1 sleep bits have no effect until SCI_EN is set.
 */
__PRIVILEGED_CODE static __hint_inline__ void enable_acpi_mode(const fadt_table_t* fadt, const pm1_block_t* pm1a) {
    if (pm1_read(pm1a) & PM1_SCI_EN) {
        return;
    }

    if (fadt->smi_command_port == 0 || fadt->acpi_enable == 0) {
        return;
    }

    x64_outb((u16)fadt->smi_command_port, fadt->acpi_enable);

    for (u32 i = 0; i < SCI_EN_POLL_ATTEMPTS; i++) {
        if (pm1_read(pm1a) & PM1_SCI_EN) {
            return;
        }
        usleep(SCI_EN_POLL_DELAY_US);
    }

    serial_write_str("power: SCI_EN did not latch after the ACPI enable command");
}

__PRIVILEGED_CODE i32 shutdown() {
    u8 typ_a = 0;
    u8 typ_b = 0;
    if (!lookup_s5_sleep_types(&fadt_data, &typ_a, &typ_b)) {
        serial_write_str("power: _S5 sleep values not found, cannot power off");
        return -1;
    }

    pm1_block_t pm1a = resolve_pm1_cnt(FADT_HAS_FIELD(&fadt_data, x_pm1a_ctrl_block) ? &fadt_data.x_pm1a_ctrl_block : NULL, fadt_data.pm1a_control_block);
    if (!pm1a.valid) {
        serial_write_str("power: PM1a control block unavailable");
        return -1;
    }

    pm1_block_t pm1b = resolve_pm1_cnt(FADT_HAS_FIELD(&fadt_data, x_pm1b_ctrl_block) ? &fadt_data.x_pm1b_ctrl_block : NULL, fadt_data.pm1b_control_block);

    asm volatile("cli");
    enable_acpi_mode(&fadt_data, &pm1a);

    // enter S5: write SLP_TYP with SLP_EN to PM1a, then PM1b if present
    uint16_t val = pm1_read(&pm1a);
    val &= (u16)(~(PM1_SLP_TYP_MASK | PM1_SLP_EN));
    pm1_write(&pm1a, val | (u16)(typ_a << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);

    if (pm1b.valid) {
        val = pm1_read(&pm1b);
        val &= (u16)(~(PM1_SLP_TYP_MASK | PM1_SLP_EN));
        pm1_write(&pm1b, val | (u16)(typ_b << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);
    }

    usleep(MECHANISM_WAIT_US);

    serial_write_str("power: ACPI S5 entry had no effect");
    return -1;
}

__PRIVILEGED_CODE i32 reboot() {
    asm volatile("cli");

    acpi_reset();
    usleep(MECHANISM_WAIT_US);

    kbc_reset();
    usleep(MECHANISM_WAIT_US);

    x64_outb(RESET_CTRL_PORT, RESET_CTRL_ARM);
    x64_outb(RESET_CTRL_PORT, RESET_CTRL_FIRE);
    usleep(MECHANISM_WAIT_US);

    triple_fault();
    return -1;
}