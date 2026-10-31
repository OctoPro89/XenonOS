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

__PRIVILEGED_CODE fadt_table_t* fadt_get() {
    if (!fadt_initialized) {
        return NULL;
    }

    return &fadt_data;
}