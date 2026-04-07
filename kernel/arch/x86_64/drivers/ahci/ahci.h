#pragma once
#include <xlibc/xstdint.h>
#include <arch/x86_64/drivers/pci/pci.h>

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
} HBA_MEM;

typedef volatile struct {
    uint32_t clb;       // command list base (low)
    uint32_t clbu;      // high
    uint32_t fb;        // FIS base
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} HBA_PORT;

typedef struct {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;

    uint8_t  r:1;
    uint8_t  b:1;
    uint8_t  c:1;
    uint8_t  rsv0:1;
    uint8_t  pmp:4;

    uint16_t prdtl;
    uint32_t prdbc;

    uint32_t ctba;
    uint32_t ctbau;

    uint32_t rsv1[4];
} HBA_CMD_HEADER;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;

    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} HBA_PRDT_ENTRY;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];
} HBA_CMD_TBL;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;

    uint8_t command;
    uint8_t featurel;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;

    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;

    uint8_t rsv1[4];
} FIS_REG_H2D;

void ahci_init(PCI_Device* dev);
void ahci_probe_ports();
HBA_PORT* ahci_get_port(int port);
int ahci_port_type(HBA_PORT* port);
void ahci_stop_port(HBA_PORT* port);
void ahci_start_port(HBA_PORT* port);
void ahci_port_init(HBA_PORT* port);
int ahci_port_has_device(HBA_PORT* port);
int ahci_find_cmdslot(HBA_PORT* port);
int ahci_read_sector(HBA_PORT* port, uint64_t lba, void* buffer);
int ahci_block_read(void* drv, uint64_t lba, uint32_t count, void* buffer);