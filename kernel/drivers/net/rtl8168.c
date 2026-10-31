#include <drivers/net/rtl8168.h>
#include <arch/x86_64/sync/sync.h>
#include <hw/mmio.h>
#include <kernel.h>

struct rx_batch_entry {
    u16 idx;
    u16 len;
};

struct rx_batch {
    struct rx_batch_entry entries[RX_BATCH_MAX];
    u32 count;
};

// largest frame the rings carry, a 1500 byte payload plus the Ethernet header
static const size_t ETH_FRAME_MAX = 1514;

// the hardware reports frame lengths with the trailing checksum included
static const u32 ETH_FCS_LEN = 4;

static __hint_inline__ u8 rtl8168_driver_reg_read8(rtl8168_driver_t* driver, u16 offset) {
    return mmio_read8(driver->mmio_va + offset);
}

static __hint_inline__ u16 rtl8168_driver_reg_read16(rtl8168_driver_t* driver, u16 offset) {
    return mmio_read16(driver->mmio_va + offset);
}

static __hint_inline__ u32 rtl8168_driver_reg_read32(rtl8168_driver_t* driver, u16 offset) {
    return mmio_read32(driver->mmio_va + offset);
}

static __hint_inline__ void rtl8168_driver_reg_write8(rtl8168_driver_t* driver, u16 offset, u8 value) {
    mmio_write8(driver->mmio_va + offset, value);
}

static __hint_inline__ void rtl8168_driver_reg_write16(rtl8168_driver_t* driver, u16 offset, u16 value) {
    mmio_write16(driver->mmio_va + offset, value);
}

static __hint_inline__ void rtl8168_driver_reg_write32(rtl8168_driver_t* driver, u16 offset, u32 value) {
    mmio_write32(driver->mmio_va + offset, value);
}

static rtl8168_chip_version_t rtl8168_driver_identify_chip(rtl8168_driver_t* driver) {
    u32 tcr = rtl8168_driver_reg_read32(driver, REG_TCR);
    u32 xid = (tcr >> TCR_HWVERID_SHIFT) & TCR_XID_MASK;

    switch (xid) {
        case 0x380: return RTL8168_CHIP_VERSION_RTL8168B_1;
        case 0x3C0: return RTL8168_CHIP_VERSION_RTL8168C_1;
        case 0x3C4: return RTL8168_CHIP_VERSION_RTL8168C_2;
        case 0x3C8: return RTL8168_CHIP_VERSION_RTL8168C_3;
        case 0x3CC: return RTL8168_CHIP_VERSION_RTL8168CP_2;
        case 0x281: return RTL8168_CHIP_VERSION_RTL8168D_1;
        case 0x2C1: return RTL8168_CHIP_VERSION_RTL8168D_2;
        case 0x288: return RTL8168_CHIP_VERSION_RTL8168DP_1;
        case 0x2C8: return RTL8168_CHIP_VERSION_RTL8168DP_2;
        case 0x2C2: return RTL8168_CHIP_VERSION_RTL8168E_1;
        case 0x2C6: return RTL8168_CHIP_VERSION_RTL8168E_2;
        case 0x481: return RTL8168_CHIP_VERSION_RTL8168F_1;
        case 0x4C1: return RTL8168_CHIP_VERSION_RTL8168F_2;
        case 0x4C0: return RTL8168_CHIP_VERSION_RTL8168G_1;
        case 0x540: return RTL8168_CHIP_VERSION_RTL8168G_2;
        case 0x541: return RTL8168_CHIP_VERSION_RTL8168H_1;
        case 0x5C0: return RTL8168_CHIP_VERSION_RTL8168H_2;
        case 0x5C8: return RTL8168_CHIP_VERSION_RTL8168FP_1;
        default: {
            printf("[RTL8168]: Unknown chip XID 0x%03x, using generic path\n", xid);
            return RTL8168_CHIP_VERSION_UNKNOWN;
        }
    }
}

// As per datasheet section 2.9: EEM=11 (0xC0) unlocks CONFIG0-5 writes
static __hint_inline__ void rtl8168_driver_config_unlock(rtl8168_driver_t* driver) {
    rtl8168_driver_reg_write8(driver, REG_9346CR, CFG_9346_UNLOCK);
}

static __hint_inline__ void rtl8168_driver_config_lock(rtl8168_driver_t* driver) {
    rtl8168_driver_reg_write8(driver, REG_9346CR, CFG_9346_LOCK);
}

// As per datasheet section 2.3: set CMD.RST, poll until self-cleared. Preserves MAC address, MAR, and PCI config space
static __hint_inline__ void rtl8168_driver_hw_reset(rtl8168_driver_t* driver) {
    rtl8168_driver_reg_write8(driver, REG_CMD, CMD_RST);

    for (u32 i = 0; i < 1000; ++i) {
        if ((rtl8168_driver_reg_read8(driver, REG_CMD) & CMD_RST) == 0) {
            printf("[RTL8168]: Hardware reset complete (%u interactions)\n");
            return;
        }

        for (u32 j = 0; j < 100; ++j) { asm volatile("pause");  }
    }

    printf("[RTL8168]: Hardware reset timeout (CMD still has RST set)\n");
}

/*
// Read MAC from IDR0-IDR5 (EEPROM autoloaded at power-on), falls back to a fixed address if EEPROM is blank
static __hint_inline__ void rtl8168_driver_read_mac_address(rtl8168_driver_t* driver) {
    u32 idr0 = rtl8168_driver_reg_read32(driver, REG_IDR0);
    u32 idr4 = rtl8168_driver_reg_read32(driver, REG_IDR4);

    driver->mac.bytes[0] = (u8)(idr0 & 0xFF);
    driver->mac.bytes[1] = (u8)((idr0 >> 8) & 0xFF);
    driver->mac.bytes[2] = (u8)((idr0 >> 16) & 0xFF);
    driver->mac.bytes[3] = (u8)((idr0 >> 24) & 0xFF);
    driver->mac.bytes[4] = (u8)(idr4 & 0xFF);
    driver->mac.bytes[5] = (u8)((idr4 >> 8) & 0xFF);

    b8 all_zero = true, all_ff = true;
    for (size_t i = 0; i < NET_ETHERNET_MAC_ADDR_LEN; i++) {
        if (driver->mac.bytes[i] != 0x00) all_zero = false;
        if (driver->mac.bytes[i] != 0xFF) all_ff = false;
    }
    if (all_zero || all_ff) {
        driver->mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
        printf("[RTL8168]: EEPROM MAC invalid, using fallback\n");
    }
}

// As per datasheet section 2.16: PHYAR at offset 0x60. FreeBSD mandates a 20us inter-operation delay.
static __hint_inline b8 rtl8168_driver_phy_read(rtl8168_driver_t* driver, u8 reg, u16* out) {
    u32 cmd = ((u32)(reg) << PHYAR_REG_SHIFT) & PHYAR_REG_MASK;
    rtl8168_driver_reg_write32(driver, REG_PHYAR, cmd);

    for (uint32_t i = 0; i < 2000; i++) {
        usleep(1);
        uint32_t val = rtl8168_driver_reg_read32(driver, REG_PHYAR);
        if (val & PHYAR_FLAG) {
            *out = (u16)(val & PHYAR_DATA_MASK);
            usleep(20);
            return false;
        }
    }

    printf("[RTL8168]: PHY read timeout (reg=0x%02x)\n", reg);
    return true;
}
*/

b8 rtl8168_driver_init_driver(rtl8168_driver_t* driver) {
    if (!driver) {
        return false;
    }
    
    spin_lock_init(&driver->lock);

    return true;
}

