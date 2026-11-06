#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <drivers/net/ethernet.h>
#include <atomics.h>

#define NET_IFACE_NAME_MAX ((size_t)16)
#define NET_MAX_INTERFACES ((size_t)8)

typedef struct net_interface {
    u64 id; // nonzero and unique for the life of the kernel, 0 means no interface
    b8 enabled; // administratively up, check by the stack before frames move either way
    b8 loopback; // frames send through it come back to the this host, so no link or ARP
    atomic_b8 link_up; // carrier as last reported by the driver
    char name[NET_IFACE_NAME_MAX];
    struct {
        u64 frames_in;
        u64 frames_out;
        u64 bytes_in;
        u64 bytes_out;
        atomic_u64 drops; // frames discarded by policy, such as a full ring
        atomic_u64 errors; // frames the hardware or the stack could not process
    } counters;

    ethernet_mac_address_t mac; // link layer identity, filled in by the driver once the hardware reports it
    u16 mtu; // largest payload carried in one frame, excluding the link header
} net_interface_t;

