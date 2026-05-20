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
        struct __packed__ {
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
        struct __packed__ {
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

typedef struct __packed__ {
    union {
        struct __packed__ {
            u8 id;
            u8 next;
            u16 cap_specific;
        };

        // extended capability entries must be read as 32-bit words
        u32 raw;
    };
} xhci_extended_capability_entry_t;

STATIC_ASSERT(sizeof(xhci_extended_capability_entry_t) == 4);

// xHCI spec station 7.0 table 7-2: xHCI extended capability codes
typedef enum {
  XHCI_EXTENDED_CAPABILITY_CODE_RESERVED = 0,
  XHCI_EXTENDED_CAPABILITY_CODE_USB_LEGACY_SUPPORT = 1,
  XHCI_EXTENDED_CAPABILITY_CODE_SUPPORTED_PROTOCOL = 2,
  XHCI_EXTENDED_CAPABILITY_CODE_EXTENDED_POWER_MANAGEMENT = 3,
  XHCI_EXTENDED_CAPABILITY_CODE_IOVIRTUALIZATION_SUPPORT = 4,
  XHCI_EXTENDED_CAPABILITY_CODE_MESSAGE_INTERRUPT_SUPPORT = 5,
  XHCI_EXTENDED_CAPABILITY_CODE_LOCAL_MEMORY_SUPPORT = 6,
  XHCI_EXTENDED_CAPABILITY_CODE_USB_DEBUG_CAPABILITY_SUPPORT = 10,
  XHCI_EXTENDED_CAPABILITY_CODE_EXTENDED_MESSAGE_INTERRUPT_SUPPORT = 17  
} xhci_extended_capability_code;

typedef struct xhci_extended_capability {
    volatile u32* base;
    xhci_extended_capability_entry_t entry;
    struct xhci_extended_capability* next;
} xhci_extended_capability_t;

// check XHCI spec if needed for more info
typedef struct __packed__ {
    union {
        struct __packed__ {
            u32 ccs         : 1;
            u32 ped         : 1;
            u32 rsvd0       : 1;
            u32 oca         : 1;
            u32 pr          : 1;
            u32 pls         : 4;
            u32 pp          : 1;
            u32 port_speed  : 4;
            u32 pic         : 2;
            u32 lws         : 1;
            u32 csc         : 1;
            u32 pec         : 1;
            u32 wrc         : 1;
            u32 occ         : 1;
            u32 prc         : 1;
            u32 plc         : 1;
            u32 cec         : 1;
            u32 cas         : 1;
            u32 wce         : 1;
            u32 wde         : 1;
            u32 woe         : 1;
            u32 rsvd1       : 2;
            u32 dr          : 1;
            u32 wpr         : 1;
        };

        u32 raw;
    };
} xhci_portsc_register_t;

STATIC_ASSERT(sizeof(xhci_portsc_register_t) == sizeof(u32));

typedef struct __packed__ {
    union {
        struct __packed__ {
            u32 link_error_count    : 16;
            u32 rx_lane_count       : 4;
            u32 tx_lane_count       : 4;
            u32 rsvd                : 8;
        };

        u32 raw;
    };
} xhci_portli_register_t;

STATIC_ASSERT(sizeof(xhci_portli_register_t) == sizeof(u32));

typedef struct __packed__ {
    union {
        struct __packed__ {
            u32 hirdm       : 2;
            u32 l1timeout   : 8;
            u32 besld       : 4;
            u32 rsvd        : 18;
        };

        u32 raw;
    };
} xhci_porthlpmc_register_usb2_t;

STATIC_ASSERT(sizeof(xhci_porthlpmc_register_usb2_t) == sizeof(u32));

typedef struct __packed__ {
    union {
        struct __packed__ {
            u16 link_soft_error_count;
            u16 rsvd;
        };

        u32 raw;
    };
} xhci_porthlpmc_register_usb3_t;

STATIC_ASSERT(sizeof(xhci_porthlpmc_register_usb3_t) == sizeof(u32));

typedef struct __packed__ {
    union {
        struct __packed__ {
            u32 l1status                        : 3;
            u32 remote_wake_enable              : 1;
            u32 host_initiated_resume_duration  : 4;
            u32 l1device_slot                   : 8;
            u32 hardware_lpm_enable             : 1;
            u32 rsvd                            : 11;
            u32 port_test_control               : 4;
        };

        u32 raw;
    };
} xhci_portpmsc_register_usb2_t;

STATIC_ASSERT(sizeof(xhci_portpmsc_register_usb2_t) == sizeof(u32));

typedef struct __packed__ {
    union {
        struct __packed__ {
            u32 u1timeout               : 8;
            u32 u2timeout               : 8;
            u32 force_link_pm_accept    : 1;
            u32 rsvd                    : 15;
        };

        u32 raw;
    };
} xhci_portpmsc_register_usb3_t;
STATIC_ASSERT(sizeof(xhci_portpmsc_register_usb2_t) == sizeof(u32));

/**
 * @note Target Value = 2 + (ZeroBasedEndpoint * 2) + (IsOutEp ? 0 : 1)
 */
xhci_doorbell_manager_t xhci_doorbell_manager_init(vaddr_t base);
void xhci_doorbell_manager_ring_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell, u8 target);
void xhci_doorbell_manager_ring_command_doorbell(xhci_doorbell_manager_t* db_manager);
void xhci_doorbell_manager_ring_control_endpoint_doorbell(xhci_doorbell_manager_t* db_manager, u8 doorbell);

xhci_extended_capability_t xhci_extended_capability_init(volatile u32* cap_ptr);