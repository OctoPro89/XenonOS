#pragma once

#include <drivers/net/rtl8168_regs.h>
// #include <net/interface.h>
#include <arch/x86_64/sync/sync.h>
#include <memory/memory_types.h>

#define RX_BATCH_MAX ((u32)32)
#define RX_PASSES_PER_WAKEUP ((u32)(RX_DESC_COUNT / RX_BATCH_MAX))

// TODO: pci driver interface, net driver interface

/**
 * @brief Realtek RTL8111/RTL8168 PCIe Gigabit Ethernet driver.
 * @note Uses MMIO BAR 2, DMA descriptor rings, and integrated PHY via PHYAR.
 */
typedef struct {
    vaddr_t mmio_va;
    rtl8168_chip_version_t chip_version;

    b8 link_up;
    u16 speed;
    b8 full_duplex;

    rtl8168_tx_desc_t* tx_ring;
    paddr_t tx_ring_phys;
    u32 tx_prod;
    u32 tx_cons;
    u32 tx_queued;
    vaddr_t tx_buf_vaddr;
    paddr_t tx_buf_phys;

    rtl8168_rx_desc_t* rx_ring;
    paddr_t rx_ring_phys;
    u32 rx_cur;
    vaddr_t rx_buf_vaddr;
    paddr_t rx_buf_phys;

    spinlock_t lock;
    b8 has_msi;
    u16 m_imr;
} rtl8168_driver_t;

b8 rtl8168_driver_init_driver(rtl8168_driver_t* driver);