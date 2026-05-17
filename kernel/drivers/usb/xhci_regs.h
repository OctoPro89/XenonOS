#pragma once
#include <memory/memory_types.h>
#include <xlibc/xstdint.h>
#include <xlibc/xassert.h>
#include <kernel.h>

typedef struct __packed__ {
    u8 caplength; // capability register length
    u8 reserved0;
    u16 hciversion;
    u32 hcsparams1;
    u32 hcsparams2;
    u32 hcsparams3;
    u32 hccparams1;
    u32 dboff;
    u32 rtsopff;
    u32 hccparams2;
} xhci_capability_registers_t;

STATIC_ASSERT(sizeof(xhci_capability_registers_t) == 32);

typedef struct __packed__ {
    u32 usbcmd;             // USB command
    u32 usbsts;             // USB status
    u32 pagesize;           // page size
    u32 reserved0[2];
    u32 dnctrl;             // device notification control
    u64 crcr;               // command ring control
    u32 reserved1[4];
    u64 dcbaap;             // device context base address array pointer
    u32 config;             // configure
    u32 reserved2[49];
    // port register set offset has to be calculated dynamically based on MAXPORTS
} xhci_operational_registers_t;

STATIC_ASSERT(sizeof(xhci_operational_registers_t) == 256);

typedef struct __packed__ {
    u32 iman;   // interrupter managemnet
    u32 imod;   // interrupter moderation
    u32 erstsz; // event ring segment table size
    u32 rsvd;   // reserved
    u64 erstba; // event ring segment table base address
    union {
        struct {
            u64 dequeue_erst_segment_index  : 3;
            u64 event_handler_busy          : 1;
            u64 event_ring_dequeue_pointer  : 60;
        };
        u64 erdp; // event ring dequeue pointer
    };
} xhci_interrupter_registers_t;

typedef struct {
    u32 mf_index;   // microframe index
    u32 rsvd[7];    // reserved
    xhci_interrupter_registers_t ir[1024]; // interrupter register sets
} xhci_runtime_registers_t;

typedef struct __packed__ {
    union {
        struct {
            u8 db_target;
            u8 rsvd;
            u16 db_stream_id;
        };

        // must be accessed using 32-bit dwords
        u32 raw;
    };
} xhci_doorbell_register_t;

typedef struct {
    xhci_doorbell_register_t* doorbell_registers;
} xhci_doorbell_manager_t;

/**
 * @note Target Value = 2 + (ZeroBasedEndpoint * 2) + (IsOutEp ? 0 : 1)
 */
xhci_doorbell_manager_t xhci_doorbell_manager_init(vaddr_t base);
void xhci_doorbell_manager_ring_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell, u8 target);
void xhci_doorbell_manager_ring_command_doorbell(xhci_doorbell_manager_t* db_manager);
void xhci_doorbell_manager_ring_control_endpoint_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell);