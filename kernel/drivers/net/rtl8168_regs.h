#pragma once

/**
 * Realtek RTL8111 / RTL8168 PCIe Gigabit Ethernet register definitions
 * PCI ID: 10EC:8168. The RTL8111 (desktop) and RTL8168 (mobile) share
 * the same programming model. All registers are MMIO-mapped via BAR 2
 * (64-bit memory BAR at PCI config offset 0x18).
 * References:
 *   - StelluxOS
 *   - RTL8111B/RTL8168B Registers Datasheet Rev 1.0 (2006-01-26)
 *   - Linux kernel drivers/net/ethernet/realtek/r8169_main.c
 *   - FreeBSD sys/dev/re/if_re.c, sys/dev/rl/if_rlreg.h
*/

// TODO: prefixes

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>
#include <kernel.h>
#include <drivers/net/phy_regs.h>

// PCI identification

#define RTL_VENDOR_ID             ((u16)0x10EC)
#define RTL_DEVICE_ID_8168        ((u16)0x8168)

// BAR index: RTL8168 uses a 64-bit memory BAR starting at BAR 2
// (PCI config offset 0x18). This consumes BAR indices 2 and 3.
#define RTL_MMIO_BAR              ((u8)2)

// MAC registers (offsets from MMIO base)
//
// Cross-referenced: datasheet 2.1, Linux enum rtl_registers,
// FreeBSD RL_IDR0 etc.

// MAC address (IDR0-IDR5). Must be read/written as 32-bit aligned accesses.
#define REG_IDR0                  ((u16)0x0000)  // ID register 0 (MAC byte 0)
#define REG_IDR4                  ((u16)0x0004)  // ID register 4 (MAC bytes 4-5)

// Multicast address register (MAR0-MAR7). 32-bit aligned access only.
#define REG_MAR0                  ((u16)0x0008)  // Multicast bits [63:32]
#define REG_MAR4                  ((u16)0x000C)  // Multicast bits [31:0]

// Dump Tally Counter Command (64-byte aligned physical address + command bit)
#define REG_DTCCR                 ((u16)0x0010) // 64-bit

// TX descriptor start addresses (256-byte aligned, 64-bit)
#define REG_TNPDS                 ((u16)0x0020)  // TX Normal Priority Descriptor Start
#define REG_THPDS                 ((u16)0x0028)  // TX High Priority Descriptor Start

// Command register
#define REG_CMD                   ((u16)0x0037) // 8-bit
#define CMD_RST                   ((u8)(1u << 4))
#define CMD_RE                    ((u8)(1u << 3))  // Receiver Enable
#define CMD_TE                    ((u8)(1u << 2))  // Transmitter Enable

// Transmit Priority Polling
#define REG_TPPOLL               ((u16)0x0038) // 8-bit, write-only
#define TPPOLL_HPQ               ((u8)(1u << 7))  // High Priority Queue poll
#define TPPOLL_NPQ               ((u8)(1u << 6))  // Normal Priority Queue poll
#define TPPOLL_FSWINT            ((u8)(1u << 0))  // Forced Software Interrupt

// Interrupt Mask Register
#define REG_IMR                   ((u16)0x003C) // 16-bit
// Interrupt Status Register
#define REG_ISR                   ((u16)0x003E) // 16-bit

// Shared IMR/ISR bit definitions
#define INT_TIMEOUT               ((u16)(1u << 14))
#define INT_FEMP                  ((u16)(1u << 9))   // RX FIFO empty after full
#define INT_SWINT                 ((u16)(1u << 8))   // Software interrupt
#define INT_TDU                   ((u16)(1u << 7))   // TX Descriptor Unavailable
#define INT_FOVW                  ((u16)(1u << 6))   // RX FIFO Overflow
#define INT_LINKCHG               ((u16)(1u << 5))   // Link Change
#define INT_RDU                   ((u16)(1u << 4))   // RX Descriptor Unavailable
#define INT_TER                   ((u16)(1u << 3))   // TX Error
#define INT_TOK                   ((u16)(1u << 2))   // TX OK
#define INT_RER                   ((u16)(1u << 1))   // RX Error
#define INT_ROK                   ((u16)(1u << 0))   // RX OK

// Aggregate masks for driver use
#define INT_MASK_TX               ((u16)INT_TOK | INT_TER | INT_TDU)
#define INT_MASK_RX               ((u16)INT_ROK | INT_RER | INT_RDU | INT_FOVW)
#define INT_MASK_LINK             ((u16)INT_LINKCHG)
#define INT_MASK_DEFAULT          ((u16)INT_MASK_TX | INT_MASK_RX | INT_MASK_LINK | INT_TIMEOUT)

// Transmit Configuration Register
#define REG_TCR                   ((u16)0x0040) // 32-bit
#define TCR_HWVERID_MASK          ((u32)0x7CF00000) // bits [30:28, 26, 23, 22:20]
#define TCR_IFG_MASK              ((u32)0x03080000) // IFG[2:0]
#define TCR_IFG_DEFAULT           ((u32)0x03000000) // IFG=011 (96ns at 1G)
#define TCR_MXDMA_MASK            ((u32)0x00000700) // Max DMA burst size
#define TCR_MXDMA_UNLIMITED       ((u32)0x00000700) // 111 unlimited
#define TCR_NOCRC                 ((u32)(1u << 16))

// Receive Configuration Register
#define REG_RCR                   ((u16)0x0044) // 32-bit
#define RCR_RXFTH_MASK            ((u32)0x0000E000) // RX FIFO threshold
#define RCR_RXFTH_NONE            ((u32)0x0000E000) // 111 no threshold
#define RCR_MXDMA_MASK            ((u32)0x00000700) // Max DMA burst size
#define RCR_MXDMA_UNLIMITED       ((u32)0x00000700) // 111 unlimited
#define RCR_9356SEL               ((u32)(1u << 6))
#define RCR_AER                   ((u32)(1u << 5))  // Accept Error Packets
#define RCR_AR                    ((u32)(1u << 4))  // Accept Runt
#define RCR_AB                    ((u32)(1u << 3))  // Accept Broadcast
#define RCR_AM                    ((u32)(1u << 2))  // Accept Multicast
#define RCR_APM                   ((u32)(1u << 1))  // Accept Physical Match
#define RCR_AAP                   ((u32)(1u << 0))  // Accept All Packets

// Timer Count Register (125 MHz internal clock -> 8ns per tick)
#define REG_TCTR                  ((u16)0x0048) // 32-bit

// 93C46/93C56 EEPROM Command Register
#define REG_9346CR                ((u16)0x0050)  // 8-bit
#define CFG_9346_LOCK             ((u8)0x00)    // EEM=00 normal mode (locked)
#define CFG_9346_UNLOCK           ((u8)0xC0)    // EEM=11 config write enable

// Configuration registers 0-5
#define REG_CONFIG0               ((u16)0x0051)
#define REG_CONFIG1               ((u16)0x0052)
#define REG_CONFIG2               ((u16)0x0053)
#define REG_CONFIG3               ((u16)0x0054)
#define REG_CONFIG4               ((u16)0x0055)
#define REG_CONFIG5               ((u16)0x0056)

// Timer interrupt register
#define REG_TIMERINT              ((u16)0x0058)

// PHY Access Register (indirect MDIO via MMIO)
#define REG_PHYAR                 ((u16)0x0060) // 32-bit
#define PHYAR_FLAG                ((u32)(1u << 31)) // 1=write, auto-clears 0=read, auto-sets
#define PHYAR_REG_SHIFT           ((u32)16)
#define PHYAR_REG_MASK            ((u32)0x001F0000)
#define PHYAR_DATA_MASK           ((u32)0x0000FFFF)

// PHY Status Register (real-time, updated every ~300us)
#define REG_PHYSTATUS             ((u16)0x006C) // 8-bit
#define PHYSTS_TXFLOW             ((u8)(1u << 6))
#define PHYSTS_RXFLOW             ((u8)(1u << 5))
#define PHYSTS_1000MF             ((u8)(1u << 4)) // 1000 Mbps full-duplex
#define PHYSTS_100M               ((u8)(1u << 3))
#define PHYSTS_10M                ((u8)(1u << 2))
#define PHYSTS_LINK               ((u8)(1u << 1))
#define PHYSTS_FULLDUP            ((u8)(1u << 0))

// Extended Register Access (ERIAR), used by 8168g+ chips
#define REG_ERIAR                 ((u16)0x00DC) // 32-bit
#define ERIAR_FLAG                ((u32)(1u << 31))
#define ERIAR_WRITE               ((u32)0x80000000)
#define ERIAR_READ                ((u32)0x00000000)
#define ERIAR_ADDR_MASK           ((u32)0x0000FFFF)
#define ERIAR_TYPE_SHIFT          ((u32)16)

// Extended Register Data
#define REG_ERIDR                 ((u16)0x00D0) // 32-bit

// RX packet Maximum Size
#define REG_RMS                   ((u16)0x00DA)  // 16-bit
#define RMS_MAX                   ((u16)0x3FFF)  // 14 bits, max 16383

// C+ Command Register
#define REG_CPCR                  ((u16)0x00E0) // 16-bit (word access only)
#define CPCR_RXVLAN               ((u16)(1u << 6))  // VLAN de-tag on RX
#define CPCR_RXCHKSUM             ((u16)(1u << 5))  // RX checksum offload

// RX Descriptor Start Address (256-byte aligned, 64-bit)
#define REG_RDSAR                 ((u16)0x00E4) // low 32 bits at E4, high at E8

// Max Transmit Packet Size (in 128-byte units)
#define REG_MTPS                  ((u16)0x00EC)  // 8-bit
#define MTPS_NORMAL               ((u8)0x0C)    // >1518 bytes (0x0C * 128 1536)
#define MTPS_JUMBO                ((u8)0x3B)    // >=7440 bytes

// Miscellaneous register (used on 8168G+ for RXDV gating)
#define REG_MISC                  ((u16)0x00F0) // 32-bit
#define MISC_RXDV_GATED           ((u32)(1u << 19))
#define MISC_EARLY_TALLY_EN       ((u32)(1u << 16))
#define MISC_PWM_EN               ((u32)(1u << 22))

// Chip version identification
//
// The hardware version is encoded in TxConfig[30:28,26,23,22:20].

#define TCR_HWVERID_SHIFT         ((u32)20)
#define TCR_XID_MASK              ((u32)0x000007CF) // bits [30:28,26,23,22:20] after >> 20

// Common RTL8168 chip versions.
// NOTE: this is a clang C extension. this probably doesn't work with GCC
// TODO: fix
typedef enum : u16 {
    RTL8168_CHIP_VERSION_UNKNOWN               = 0x000,
    RTL8168_CHIP_VERSION_RTL8168B_1            = 0x380,  // 8168B, early
    RTL8168_CHIP_VERSION_RTL8168B_2            = 0x380,  // 8168Bp
    RTL8168_CHIP_VERSION_RTL8168C_1            = 0x3C0,  // 8168C, early
    RTL8168_CHIP_VERSION_RTL8168C_2            = 0x3C4,  // 8168C
    RTL8168_CHIP_VERSION_RTL8168C_3            = 0x3C8,  // 8168C
    RTL8168_CHIP_VERSION_RTL8168CP_1           = 0x3C8,  // 8168CP (shares XID with 8168C_3)
    RTL8168_CHIP_VERSION_RTL8168CP_2           = 0x3CC,  // 8168CP
    RTL8168_CHIP_VERSION_RTL8168D_1            = 0x281,  // 8168D
    RTL8168_CHIP_VERSION_RTL8168D_2            = 0x2C1,  // 8168D
    RTL8168_CHIP_VERSION_RTL8168DP_1           = 0x288,  // 8168DP (DASH)
    RTL8168_CHIP_VERSION_RTL8168DP_2           = 0x2C8,  // 8168DP
    RTL8168_CHIP_VERSION_RTL8168E_1            = 0x2C2,  // 8168E (VL)
    RTL8168_CHIP_VERSION_RTL8168E_2            = 0x2C6,  // 8168E-VL
    RTL8168_CHIP_VERSION_RTL8168F_1            = 0x481,  // 8168F
    RTL8168_CHIP_VERSION_RTL8168F_2            = 0x4C1,  // 8168F
    RTL8168_CHIP_VERSION_RTL8168G_1            = 0x4C0,  // 8168G
    RTL8168_CHIP_VERSION_RTL8168G_2            = 0x540,  // 8168GU/8111GU
    RTL8168_CHIP_VERSION_RTL8168H_1            = 0x541,  // 8168H
    RTL8168_CHIP_VERSION_RTL8168H_2            = 0x5C0,  // 8168H
    RTL8168_CHIP_VERSION_RTL8168FP_1           = 0x5C8   // 8168FP (DASH)
} rtl8168_chip_version_t;

// 8168G and later chips use the RXDV gate to block RX data during
// initialization. XIDs are not monotonically ordered by generation,
// so check explicitly rather than using a >comparison
static __hint_inline__ b8 chip_is_8168g_plus(rtl8168_chip_version_t ver) {
    switch (ver) {
        case RTL8168_CHIP_VERSION_RTL8168G_1:
        case RTL8168_CHIP_VERSION_RTL8168G_2:
        case RTL8168_CHIP_VERSION_RTL8168H_1:
        case RTL8168_CHIP_VERSION_RTL8168H_2:
        case RTL8168_CHIP_VERSION_RTL8168FP_1: {
            return true;
        }
        default: {
            return false;
        }
    }
}

// TX descriptor format (16 bytes)
//
// Each TX descriptor is 4 DWORDs:
//   DWORD 0 (opts1): OWN | EOR | FS | LS | flags | length
//   DWORD 1 (opts2): VLAN tag control + checksum offload
//   DWORD 2: buffer physical address low 32 bits
//   DWORD 3: buffer physical address high 32 bits
//
// The ring wraps at the descriptor where EOR is set

typedef struct __packed__ {
    u32 opts1;
    u32 opts2;
    u32 addr_lo;
    u32 addr_hi;
} rtl8168_tx_desc_t;

STATIC_ASSERT(sizeof(rtl8168_tx_desc_t) == 16);

// TX opts1 bit definitions
#define TX_OWN                    ((u32)(1u << 31))  // NIC owns descriptor
#define TX_EOR                    ((u32)(1u << 30))  // End Of Ring
#define TX_FS                     ((u32)(1u << 29))  // First Segment
#define TX_LS                     ((u32)(1u << 28))  // Last Segment
#define TX_LGSEN                  ((u32)(1u << 27))  // Large Send (TSO) enable
#define TX_IPCS                   ((u32)(1u << 18))  // IP Checksum Offload
#define TX_UDPCS                  ((u32)(1u << 17))  // UDP Checksum Offload
#define TX_TCPCS                  ((u32)(1u << 16))  // TCP Checksum Offload
#define TX_LEN_MASK               ((u32)0x0000FFFF)  // Frame length (bits [15:0])

// TX opts2 bit definitions
#define TX_TAGC                   ((u32)(1u << 17))  // Insert VLAN tag
#define TX_VLAN_MASK              ((u32)0x0000FFFF)  // VLAN tag value

// RX descriptor format (16 bytes)
//
// Each RX descriptor is 4 DWORDs:
//   DWORD 0 (opts1): OWN | EOR | status flags | frame length / buffer size
//   DWORD 1 (opts2): VLAN tag info
//   DWORD 2: buffer physical address low 32 bits
//   DWORD 3: buffer physical address high 32 bits
//
// When OWN=1 (command mode): bits [13:0] buffer size (max 0x1FF8)
// When OWN=0 (status mode):  bits [13:0] frame length including CRC

typedef struct __packed__ {
    u32 opts1;
    u32 opts2;
    u32 addr_lo;
    u32 addr_hi;
} rtl8168_rx_desc_t;

STATIC_ASSERT(sizeof(rtl8168_rx_desc_t) == 16);

// RX opts1 bit definitions (command mode, OWN=1)
#define RX_OWN                    ((u32)(1u << 31))  // NIC owns descriptor
#define RX_EOR                    ((u32)(1u << 30))  // End Of Ring
#define RX_BUF_SIZE_MASK          ((u32)0x00003FFF)  // Buffer size [13:0] max 0x1FF8

// RX opts1 bit definitions (status mode, OWN=0)
#define RX_FS                     ((u32)(1u << 29))  // First Segment
#define RX_LS                     ((u32)(1u << 28))  // Last Segment
#define RX_MAR                    ((u32)(1u << 27))  // Multicast Address match
#define RX_PAM                    ((u32)(1u << 26))  // Physical Address Match
#define RX_BAR                    ((u32)(1u << 25))  // Broadcast Address
#define RX_RWT                    ((u32)(1u << 22))  // Watchdog Timer expired
#define RX_RES                    ((u32)(1u << 21))  // Receive Error Summary
#define RX_RUNT                   ((u32)(1u << 20))  // Runt packet (<64 bytes)
#define RX_CRC                    ((u32)(1u << 19))  // CRC error

// Protocol ID field [18:17]
#define RX_PID_MASK               ((u32)(3u << 17))
#define RX_PID_NON_IP             ((u32)(0u << 17))
#define RX_PID_TCP_IP             ((u32)(1u << 17))
#define RX_PID_UDP_IP             ((u32)(2u << 17))
#define RX_PID_IP                 ((u32)(3u << 17))

#define RX_IPF                    ((u32)(1u << 16))  // IP Checksum Failure
#define RX_UDPF                   ((u32)(1u << 15))  // UDP Checksum Failure
#define RX_TCPF                   ((u32)(1u << 14))  // TCP Checksum Failure
#define RX_FRAME_LEN_MASK         ((u32)0x00003FFF)  // Frame length [13:0]

// RX opts2 bit definitions
#define RX_TAVA                   ((u32)(1u << 16)) // Tag Available (VLAN)
#define RX_VLAN_MASK              ((u32)0x0000FFFF)

// Descriptor ring parameters

#define TX_DESC_COUNT             ((u32)256)
#define RX_DESC_COUNT             ((u32)256)

// RX buffer size: must be multiple of 8, >frame size. We use 2048 to
// accommodate MTU + Ethernet header + possible VLAN + CRC with room.
#define RX_BUF_SIZE               ((u32)2048)

// Descriptor rings must be 256-byte aligned
#define DESC_RING_ALIGN           ((size_t)256)