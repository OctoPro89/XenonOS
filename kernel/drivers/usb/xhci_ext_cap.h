#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xassert.h>
#include <kernel.h>

typedef struct __packed__ {
    union {
        struct __packed__ {
            u8 id;
            u8 next;
            u8 minor_revision_version;
            u8 major_revision_version;
        };
        u32 dword0;
    };

    union {
        u32 name; // "USB "
        u32 dword1;
    };

    union {
        struct __packed__ {
            u8 compatible_port_offset;
            u8 compatible_port_count;
            u8 protocol_defined;
            u8 protocol_speed_id_count;
        };
        u32 dword2;
    };

    union {
        struct __packed__ {
            u32 slot_type : 4;
            u32 reserved : 28;
        };
        u32 dword3;
    };
} xhci_usb_supported_protocol_capability_t;

static __hint_inline__ xhci_usb_supported_protocol_capability_t xhci_usb_supported_protocol_capability_init(volatile u32* cap) {
    xhci_usb_supported_protocol_capability_t uspc;
    uspc.dword0 = cap[0];
    uspc.dword1 = cap[1];
    uspc.dword2 = cap[2];
    uspc.dword3 = cap[3];
    return uspc;
}

STATIC_ASSERT(sizeof(xhci_usb_supported_protocol_capability_t) == (sizeof(u32) * 4));