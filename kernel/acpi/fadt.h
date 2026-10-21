#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xassert.h>
#include <acpi/acpi.h>
#include <kernel.h>

typedef struct __packed__ {
    u8 address_space;
    u8 bit_width;
    u8 bit_offset;
    u8 access_size;
    u64 address;
} generic_address_structure_t;

STATIC_ASSERT(sizeof(generic_address_structure_t) == 12);

// the ACPI 1.0 fixed block ends after the flags field, later revisions appended fields, so anything past this offset needs a length check
#define FADT_REV1_LENGTH 116

// FADT fixed feature flags
#define FADT_FLAG_RESET_REG_SUP (1 << 10)

/**
 * @struct fadt_table_t
 * @brief Represents the Fixed ACPI Description Table (FADT)s
 *
 * @note The FADT contains information about power management hardware
 * and the system's sleep states. It is a key component in ACPI-compliant systems.
 */
typedef struct __packed__ {
    acpi_sdt_header_t header;

    u32 firmware_ctrl;
    u32 dsdt;

    // Field used in ACPI 1.0, but reserved in later versions
    u8 reserved;

    u8 preferred_power_management_profile;
    u16 sci_interrupt;
    u32 smi_command_port;
    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_req;
    u8 pstate_control;
    u32 pm1a_event_block;
    u32 pm1b_event_block;
    u32 pm1a_control_block;
    u32 pm1b_control_block;
    u32 pm2_control_block;
    u32 pm_timer_block;
    u32 gpe0_block;
    u32 gpe1_block;
    u8  pm1_evt_len;
    u8  pm1_ctrl_len;
    u8  pm2_ctrl_len;
    u8  pm_timer_len;
    u8  gpe0_block_len;
    u8  gpe1_block_len;
    u8 gpe1_base;
    u8 cstate_control;
    u16 worst_c2_latency;
    u16 worst_c3_latency;
    u16 flush_size;
    u16 flush_stride;
    u8 duty_offset;
    u8 duty_width;
    u8 day_alarm;
    u8 month_alarm;
    u8 century;

    // Reserved in ACPI 1.0, used since ACPI 2.0+
    u16 iapc_boot_arch_flags;

    u8 reserved2;
    u32 flags;

    generic_address_structure_t reset_reg;

    u8 reset_value;
    u8 reserved3[3];

    // 64-bit pointers available on ACPI 2.0+
    u64 x_firmware_ctrl;
    u64 x_dsdt;

    generic_address_structure_t x_pm1a_event_block;
    generic_address_structure_t x_pm1b_event_block;
    generic_address_structure_t x_pm1a_ctrl_block;
    generic_address_structure_t x_pm1b_ctrl_block;
    generic_address_structure_t x_pm2_ctrl_block;
    generic_address_structure_t x_pm_timer_block;
    generic_address_structure_t x_gpe0_block;
    generic_address_structure_t x_gpe1_block;
    generic_address_structure_t sleep_control_reg;
    generic_address_structure_t sleep_status_reg;
    u64 hypervisor_id;
} fadt_table_t;

STATIC_ASSERT(sizeof(fadt_table_t) == 276);
STATIC_ASSERT(__builtin_offsetof(fadt_table_t, century) == 108);
STATIC_ASSERT(__builtin_offsetof(fadt_table_t, flags) == 112);
STATIC_ASSERT(__builtin_offsetof(fadt_table_t, reset_reg) == FADT_REV1_LENGTH);
STATIC_ASSERT(__builtin_offsetof(fadt_table_t, x_dsdt) == 140);

/**
 * @brief Initializes the FADT global object using the provided ACPI FADT table
 * @note Processes the provided FADT table and extracts relevant power management settings.
 */
__PRIVILEGED_CODE void fadt_init(acpi_sdt_header_t* acpi_fadt_table);

/**
 * @brief Reboots the system using the ACPI reset register.
 */
// __PRIVILEGED_CODE void fadt_reboot();

/**
 * @brief Shuts down the system using ACPI S5 sleep state.
 */
// __PRIVILEGED_CODE void fadt_shutdown();

extern fadt_table_t fadt_data;

static __hint_inline__ b8 fadt_is_ps2_controller_present() { return !(fadt_data.iapc_boot_arch_flags & (1 << 1)); } 

#define FADT_HAS_FIELD(f, field) ((f)->header.length >= __builtin_offsetof(fadt_table_t, field) + sizeof((f)->field))