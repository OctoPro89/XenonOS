#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

#define ETHERNET_MAC_ADDR_LEN       ((size_t)6)
#define ETHERNET_HEADER_LEN         ((size_t)14)
#define ETHERNET_MTU                ((size_t)1500)                                          // largest payload in one frame
#define ETHERNET_MIN_FRAME_LEN      ((size_t)60)                                            // shorter frames are padded to this
#define ETHERNET_MAX_FRAME_LEN      ((size_t)ETHERNET_HEADER_LEN + ETHERNET_MTU)            // 1514
#define ETHERNET_MIN_PAYLOAD_LEN    ((size_t)ETHERNET_MIN_FRAME_LEN - ETHERNET_HEADER_LEN)  // 46

// headroom a driver reserves before copying a received frame in, two bytes of padding put the network header, which follows the 14-byte link header, on a 4-byte boundary
#define ETHERNET_RX_ALIGN_PAD       ((size_t)2)

// values of the type field, in host byte order | frames with a value below ETHERNET_TYPE_MIN carry an IEEE 802.3 payload length there instead of a type
#define ETHERNET_TYPE_MIN           0x0600
#define ETHERNET_TYPE_IPV4          0x0800
#define ETHERNET_TYPE_ARP           0x0806
#define ETHERNET_TYPE_IPV6          0x86DD

typedef struct {
    u8 bytes[ETHERNET_MAC_ADDR_LEN];
} ethernet_mac_address_t;