#include "ahci.h"
#include <paging.h>
#include <arch/x86_64/io.h>
#include <xlibc/string.h>

#define AHCI_VIRT_BASE 0xFFFF800000000000ULL

// drive types
#define SATA_SIG_ATA 0x00000101

// port control
#define HBA_PxCMD_ST  (1 << 0)
#define HBA_PxCMD_FRE (1 << 4)
#define HBA_PxCMD_FR  (1 << 14)
#define HBA_PxCMD_CR  (1 << 15)

#define HBA_PORT_DEV_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE  0x1

HBA_MEM* abar = 0;

void ahci_init(PCI_Device* dev) {
    // Enable PCI bus mastering
    u32 cmd = pci_config_read(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 2); // bus master
    cmd |= (1 << 1); // memory space
    pci_config_write(dev->bus, dev->device, dev->function, 0x04, cmd);

    // Get BAR5 (ABAR)
    u64 phys = dev->bar[5] & ~0xF;

    // Map ABAR (map 4KB for now)
    u64 virt = AHCI_VIRT_BASE;
    map_page(current_pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE);

    abar = (HBA_MEM*)virt;

    serial_write_str("AHCI mapped\n");
    serial_write_str("Version: ");
    serial_write_hex(abar->vs);
    serial_write_char('\n');
}

void ahci_probe_ports() {
    u32 pi = abar->pi;

    for (int i = 0; i < 32; i++) {
        if (abar->pi & (1 << i)) {
            HBA_PORT* p = ahci_get_port(i);

            if (ahci_port_has_device(p)) {
                serial_write_str("Active SATA port: ");
                serial_write_dec(i);
                serial_write_char('\n');

                ahci_port_init(p);
                break;
            }
        }
    }
}

HBA_PORT* ahci_get_port(int port) {
    return (HBA_PORT*)((u8*)abar + 0x100 + port * 0x80);
}

int ahci_port_type(HBA_PORT* port) {
    return port->sig;
}

void ahci_stop_port(HBA_PORT* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    while (port->cmd & HBA_PxCMD_FR);
    while (port->cmd & HBA_PxCMD_CR);
}

void ahci_start_port(HBA_PORT* port) {
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

void ahci_port_init(HBA_PORT* port) {
    ahci_stop_port(port);

    // Command list (1KB)
    void* clb = alloc_page();
    memset(clb, 0, 1024);

    port->clb = (uint32_t)(uint64_t)clb;
    port->clbu = 0;

    // FIS (256 bytes)
    void* fb = alloc_page();
    memset(fb, 0, 256);

    port->fb = (uint32_t)(uint64_t)fb;
    port->fbu = 0;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)clb;

    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 1;

        void* tbl = alloc_page();
        memset(tbl, 0, 256);

        cmdheader[i].ctba = (uint32_t)(uint64_t)tbl;
        cmdheader[i].ctbau = 0;
    }

    ahci_start_port(port);

    serial_write_str("Port initialized\n");
}

int ahci_port_has_device(HBA_PORT* port) {
    uint32_t ssts = port->ssts;

    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;

    return (det == HBA_PORT_DEV_PRESENT) && (ipm == HBA_PORT_IPM_ACTIVE);
}

int ahci_find_cmdslot(HBA_PORT* port) {
    uint32_t slots = port->sact | port->ci;

    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) {
            return i;
        }
    }

    return -1;
}

int ahci_read_sector(HBA_PORT* port, uint64_t lba, void* buffer) {
    port->is = (uint32_t)-1; // clear interrupts

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) {
        serial_write_str("No free command slot\n");
        return 0;
    }

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(uint64_t)(port->clb);
    cmdheader += slot;

    cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // command FIS size
    cmdheader->w = 0; // read
    cmdheader->prdtl = 1;

    // Command table
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(uint64_t)(
        ((uint64_t)cmdheader->ctba)
    );

    // Clear command table
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL));

    // Setup PRDT (data buffer)
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uint64_t)buffer;
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = 512 - 1; // 1 sector
    cmdtbl->prdt_entry[0].i = 1;

    // Setup FIS
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmdtbl->cfis);

    fis->fis_type = 0x27;
    fis->c = 1;

    fis->command = 0x25; // READ DMA EXT

    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->device = 1 << 6; // LBA mode

    fis->countl = 1;
    fis->counth = 0;

    // Wait until not busy
    while (port->tfd & (0x80 | 0x08));

    // Issue command
    port->ci |= (1 << slot);

    // Wait for completion
    while (1) {
        if (!(port->ci & (1 << slot))) {
            break;
        }

        if (port->is & (1 << 30)) {
            serial_write_str("Read disk error\n");
            return 0;
        }
    }

    return 1;
}

int ahci_block_read(void* drv, uint64_t lba, uint32_t count, void* buffer) {
    HBA_PORT* port = (HBA_PORT*)drv;

    for (uint32_t i = 0; i < count; i++) {
        if (!ahci_read_sector(port, lba + i, (uint8_t*)buffer + i * 512)) {
            return 0;
        }
    }
    return 1;
}