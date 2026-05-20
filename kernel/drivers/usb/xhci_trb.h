#pragma once
#include <drivers/usb/xhci_common.h>
#include <xlibc/xassert.h>
#include <kernel.h>

typedef struct __packed__ xhci_transfer_request_block {
    u64 parameter; // trb-specific parameter
    u32 status; // status information
    union {
        struct __packed__ {
            u32 cycle_bit   : 1;
            u32 rsvd0       : 9;
            u32 trb_type    : 6;
            u32 rsvd1       : 16;
        };
        u32 control; // control bits
    };
} xhci_trb_t;

STATIC_ASSERT(sizeof(xhci_trb_t) == sizeof(u32) * 4);

typedef struct __packed__ {
    u64 command_trb_pointer;
    struct __packed__ {
        u32 rsvd0           : 24;
        u32 completion_code : 8;
    };
    struct __packed__ {
        u32 cycle_bit       : 1;
        u32 rsvd1           : 9;
        u32 trb_type        : 6;
        u32 vfid            : 8;
        u32 slot_id         : 8;
    };
} xhci_command_completion_trb_t;

STATIC_ASSERT(sizeof(xhci_command_completion_trb_t) == sizeof(u32) * 4);