
#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <drivers/pci/pci.h>
#include <drivers/pci/core/pci_driver.h>

// Stream Descriptor: 0x20 bytes
typedef struct __packed__ {
    u32 ctl_sts;
    u32 lpib;
    u32 cbl;
    u16 lvi;
    u8  reserved0[2];
    u16 fifod;
    u16 fmt;
    u8  reserved1[4];
    u32 bdpl;
    u32 bdpu;
} intel_hda_stream_desc_t;

// Global HDA registers: 0x80 bytes
typedef struct __packed__ {
    u16 gcap;           // 0x00
    u8  vmin;           // 0x02
    u8  vmaj;           // 0x03
    u16 outpay;         // 0x04
    u16 inpay;          // 0x06
    u32 gctl;           // 0x08
    u16 wakeen;         // 0x0C
    u16 statests;       // 0x0E
    u16 gsts;           // 0x10

    u8 reserved0[6];    // 0x12 - 0x17

    u16 outstrmpay;     // 0x18
    u16 instrmpay;      // 0x1A

    u8 reserved1[4];    // 0x1C - 0x1F

    u32 intctl;         // 0x20
    u32 intsts;         // 0x24

    u8 reserved2[8];    // 0x28 - 0x2F

    u32 walclk;         // 0x30

    u8 reserved3[4];    // 0x34 - 0x37

    u32 ssync;          // 0x38

    u8 reserved4[4];    // 0x3C - 0x3F

    // CORB: 0x40
    u32 corblbase;      // 0x40
    u32 corbubase;      // 0x44
    u16 corbwp;         // 0x48
    u16 corbrp;         // 0x4A
    u8 corbctl;         // 0x4C
    u8 corbsts;         // 0x4D
    u8 corbsize;        // 0x4E
    u8 reserved5;       // 0x4F

    // RIRB: 0x50
    u32 rirblbase;      // 0x50
    u32 rirbubase;      // 0x54
    u16 rirbwp;         // 0x58
    u16 rintcnt;        // 0x5A
    u8 rirbctl;         // 0x5C
    u8 rirbsts;         // 0x5D
    u8 rirbsize;        // 0x5E
    u8 reserved6;       // 0x5F

    // Immediate Command: 0x60
    u32 icoi;           // 0x60
    u32 icii;           // 0x64
    u16 icis;           // 0x68
    u8 reserved7[6];    // 0x6A - 0x6F

    // DMA Position Buffer: 0x70
    u32 dmaplbase;      // 0x70
    u32 dmapubase;      // 0x74

    u8 reserved8[8];    // 0x78 - 0x7F

    // Stream descriptors begin at 0x80
    intel_hda_stream_desc_t streams[];

} intel_hda_regs_t;

typedef struct {
    INTERFACE_IMPLEMENT(IPCIDRIVER_MEMBERS, IPCIDRIVER_METHODS);

    volatile intel_hda_regs_t* regs;

    u8 num_codecs;
} intel_hda_driver_t;

_Static_assert(offsetof(intel_hda_regs_t, gcap) == 0x00, "GCAP offset");
_Static_assert(offsetof(intel_hda_regs_t, gctl) == 0x08, "GCTL offset");
_Static_assert(offsetof(intel_hda_regs_t, gsts) == 0x10, "GSTS offset");
_Static_assert(offsetof(intel_hda_regs_t, intctl) == 0x20, "INTCTL offset");
_Static_assert(offsetof(intel_hda_regs_t, corblbase) == 0x40, "CORB offset");
_Static_assert(offsetof(intel_hda_regs_t, rirblbase) == 0x50, "RIRB offset");
_Static_assert(offsetof(intel_hda_regs_t, icoi) == 0x60, "ICOI offset");
_Static_assert(offsetof(intel_hda_regs_t, dmaplbase) == 0x70, "DMAP offset");
_Static_assert(offsetof(intel_hda_regs_t, streams) == 0x80, "Streams offset");

_Static_assert(sizeof(intel_hda_stream_desc_t) == 0x20, "Stream descriptor size");

b8 intel_hda_driver_init_driver(void* self);
b8 intel_hda_driver_reset_controller(intel_hda_driver_t* driver);

IPCIDRIVER* intel_hda_driver_factory(PCI_Device* dev);