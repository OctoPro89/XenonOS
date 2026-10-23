#include <drivers/hwmon/amd_smn.h>
#include <arch/x86_64/drivers/pci/pci.h>

/*
    First pass implementation for AMD node 0:
    PCI BDF 00:18.0

    SMN index: PCI config offset 0x60
    SMN data:  PCI config offset 0x64
*/

#define AMD_SMN_BUS          0
#define AMD_SMN_DEVICE       0
#define AMD_SMN_FUNCTION     0

#define AMD_SMN_INDEX_OFFSET 0x60
#define AMD_SMN_DATA_OFFSET  0x64

#define AMD_VENDOR_ID        0x1022

static b8 amd_smn_ready = 0;

b8 amd_smn_init() {
    u16 vendor_id = pci_config_read16(AMD_SMN_BUS, AMD_SMN_DEVICE, AMD_SMN_FUNCTION, 0x00);

    if (vendor_id != AMD_VENDOR_ID) {
        amd_smn_ready = 0;
        return 0;
    }

    amd_smn_ready = 1;
    return 1;
}

b8 amd_smn_read(u32 address, u32* value) {
    if (!amd_smn_ready || !value) {
        return 0;
    }

    u64 flags;
    pci_config_lock_irqsave(&flags);

    pci_config_write32_locked(AMD_SMN_BUS, AMD_SMN_DEVICE, AMD_SMN_FUNCTION, AMD_SMN_INDEX_OFFSET, address);

    u32 result = pci_config_read32_locked(AMD_SMN_BUS, AMD_SMN_DEVICE, AMD_SMN_FUNCTION, AMD_SMN_DATA_OFFSET);

    pci_config_unlock_irqrestore(flags);

    // TODO: this is not perfect
    if (result == 0xFFFFFFFFu) {
        return 0;
    }

    *value = result;
    return 1;
}