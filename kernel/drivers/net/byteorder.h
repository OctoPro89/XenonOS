#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

// protocol fields are sent most significant byte first, RFC 1700's network byte order.
// these convert to and from host order, a byte swap on little-endian hosts
#define NET_HOST_IS_LITTLE_ENDIAN = ((b8)(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__));

static __hint_inline__ u16 net_bswap16(u16 v) {
    return (u16)((v << 8) | (v >> 8));
}

static __hint_inline__ u32 net_bswap32(u32 v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8)  | ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}

static __hint_inline__ u16 net_htons(u16 v) { return NET_HOST_IS_LITTLE_ENDIAN ? net_bswap16(v) : v; }
static __hint_inline__ u16 net_ntohs(u16 v) { return NET_HOST_IS_LITTLE_ENDIAN ? net_bswap16(v) : v; }
static __hint_inline__ u32 net_htonl(u32 v) { return NET_HOST_IS_LITTLE_ENDIAN ? net_bswap32(v) : v; }
static __hint_inline__ u32 net_ntohl(u32 v) { return NET_HOST_IS_LITTLE_ENDIAN ? net_bswap32(v) : v; }
