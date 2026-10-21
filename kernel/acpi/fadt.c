#include <acpi/fadt.h>
#include <xlibc/string.h>
#include <arch/x86_64/io.h>

fadt_table_t fadt_data;
static b8 fadt_initialized = false;

__PRIVILEGED_CODE void fadt_init(acpi_sdt_header_t* acpi_fadt_table) {
    if (!acpi_fadt_table) { return; }
    if (acpi_fadt_table->length < FADT_REV1_LENGTH) { return; }

    memcpy(&fadt_data, acpi_fadt_table, sizeof(fadt_table_t));
    fadt_initialized = true;
}

/*
__PRIVILEGED_CODE void fadt_reboot() {
    if (!fadt_initialized) { return; }

    serial_write_str("[REBOOT] Initiating reboot through FADT ACPI table.\n");

    u16 reset_port = (u16)(fadt_data.reset_reg.address);
    u8 reset_value = fadt_data.reset_value;

    x64_outb(reset_port, reset_value);
}

__PRIVILEGED_CODE void fadt_shutdown() {
    if (!fadt_initialized) { return; }
    
    serial_write_str("[SHUTDOWN] Initiating shutdown through a FADT ACPI table.\n");

    u16 SLP_EN = (1 << 13); // sleep enable bit

    x64_outw(fadt_data.pm1a_control_block, SLP_EN);
    
    if (fadt_data.pm1b_control_block) {
        x64_outw(fadt_data.pm1b_control_block, SLP_EN);
    }
}
*/