#pragma once

/**
 * Standard IEEE 802.3 MII PHY registers and Broadcom BCM54213PE shadow registers for RGMII clock delay configuration.
 */

#include <xlibc/xstdint.h>

// Basic Control Register
#define BMCR                                ((u8)0x00)
#define BMCR_RESET                          ((u16)(1u << 15))
#define BMCR_LOOPBACK                       ((u16)(1u << 14))
#define BMCR_SPEED_100                      ((u16)(1u << 13))
#define BMCR_ANE                            ((u16)(1u << 12))
#define BMCR_POWER_DOWN                     ((u16)(1u << 11))
#define BMCR_RESTART_AN                     ((u16)(1u << 9))
#define BMCR_FULL_DUPLEX                    ((u16)(1u << 8))
#define BMCR_SPEED_1000                     ((u16)(1u << 6))

// Basic Status Register
#define BMSR                                ((u8)0x01)
#define BMSR_100BASETX_FDX                  ((u16)(1u << 14))
#define BMSR_100BASETX                      ((u16)(1u << 13))
#define BMSR_10BASET_FDX                    ((u16)(1u << 12))
#define BMSR_10BASET                        ((u16)(1u << 11))
#define BMSR_ANEG_COMPLETE                  ((u16)(1u << 5))
#define BMSR_LINK_STATUS                    ((u16)(1u << 2))

// PHY Identifier registers
#define PHYIDR1                             ((u8)0x02)
#define PHYIDR2                             ((u8)0x03)

// Auto-Negotiation Advertisement Register
#define ANAR                                ((u8)0x04)
#define ANAR_PAUSE                          ((u16)(1u << 10))
#define ANAR_100BASETX_FDX                  ((u16)(1u << 8))
#define ANAR_100BASETX                      ((u16)(1u << 7))
#define ANAR_10BASET_FDX                    ((u16)(1u << 6))
#define ANAR_10BASET                        ((u16)(1u << 5))
#define ANAR_SELECTOR_8023                  ((u16)0x0001)

// Auto-Negotiation Link Partner Ability Register
#define ANLPAR                              ((u8)0x05)
// Same bit layout as ANAR

// 1000Base-T Control Register
#define GBCR                                ((u8)0x09)
#define GBCR_1000BASET_FDX                  ((u16)(1u << 9))
#define GBCR_1000BASET                      ((u16)(1u << 8))

// 1000Base-T Status Register
#define GBSR                                ((u8)0x0A0)
// Partner bits are shifted right by 2 from GBCR bits
#define GBSR_1000BASET_FDX                  ((u16)(1u << 11))
#define GBSR_1000BASET                      ((u16)(1u << 10))


// Broadcom BCM54213PE specific shadow registers
// Used for RGMII clock delay configuration.

// Auxiliary Control shadow register (register 0x18)
// Bits [2:0] select the shadow function (0x00-0x07).
#define BRGPHY_AUXCTL                       ((u8)0x18)
#define BRGPHY_AUXCTL_SHADOW_MISC           ((u16)0x07)
#define BRGPHY_AUXCTL_MISC_DATA_MASK        ((u16)0x7FF8)
#define BRGPHY_AUXCTL_MISC_READ_SHIFT       ((u16)12)
#define BRGPHY_AUXCTL_MISC_WRITE_EN         ((u16)0x8000)
#define BRGPHY_AUXCTL_MISC_RGMII_SKEW_EN    ((u16)0x0200)

// Shadow register 0x1C
// Bit 15 is write enable, bits [14:10] select function (0x00-0x1F).
#define BRGPHY_SHADOW_1C                    ((u8)0x1C)
#define BRGPHY_SHADOW_1C_WRITE_EN           ((u16)0x8000)
#define BRGPHY_SHADOW_1C_SEL_MASK           ((u16)0x7C00)
#define BRGPHY_SHADOW_1C_DATA_MASK          ((u16)0x03FF)

// Clock Alignment Control Register (shadow 0x1C, select value 0x03)
#define BRGPHY_SHADOW_1C_CLK_CTRL           ((u16)(0x03 << 10))
#define BRGPHY_SHADOW_1C_GTXCLK_EN          ((u16) 0x0200)

// PHY speed/duplex enumerations
// NOTE: this is a clang C extension. this probably doesn't work with GCC
// TODO: fix
typedef enum : u16 {
    PHY_SPEED_NONE = 0,
    PHY_SPEED_10   = 10,
    PHY_SPEED_100  = 100,
    PHY_SPEED_1000 = 1000
} phy_speed_t;

typedef enum : u8 {
    PHY_DUPLEX_HALF = 0,
    PHY_DUPLEX_FULL = 1
} phy_duplex_t;