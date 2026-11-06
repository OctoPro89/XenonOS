#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>

#define NET_PACKET_OBJECT_SIZE ((size_t)2048)
#define NET_PACKET_METADATA_SIZE ((size_t)40)
#define NET_PACKET_CAPACITY ((size_t)(NET_PACKET_OBJECT_SIZE - NET_PACKET_METADATA_SIZE))

typedef struct net_interface net_interface_t;

struct list_node {
    list_node* prev;
    list_node* next;
};

/**
 * @brief A packet is the single buffer that carries one frame through the network stack.
 * @note Headers are added in front of the payload on the way down and stripped from the front on the
 * way up, so it keeps a window `[data, tail)` inside a fixed buffer and every layer works only on that window
 * A packet has exactly one owner at any moment. It must be created with `packet_alloc`
 * and released with `packet_free`. Protocol layers own their header layouts and the helpers that
 * read and write them, the packet itself only knows bytes
 * @attention ***IMPORTANT*** DO NOT COPY
 */
typedef struct {
    // linkage for the one queue holding the packet while it waits (an ARP entry or a socket receive queue) it leaves that queue before anything else touches it
    struct list_node link;

    net_interface_t* iface;

    // window bounds and header marks are offsets into buffer
    u16 data;
    u16 tail;
    u16 link_header;
    u16 network_header;
    u16 transport_header;

    alignas(8) u8 buffer[NET_PACKET_CAPACITY];
} net_packet_t;

STATIC_ASSERT(sizeof(net_packet_t) == NET_PACKET_OBJECT_SIZE);