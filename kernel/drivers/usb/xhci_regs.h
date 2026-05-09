#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xassert.h>

typedef struct {
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
} xhci_capability_registers;

STATIC_ASSERT(sizeof(xhci_capability_registers) == 32);

typedef struct {
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
} xhci_operational_registers;

STATIC_ASSERT(sizeof(xhci_operational_registers) == 256);