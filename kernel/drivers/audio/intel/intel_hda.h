
#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>
#include <drivers/pci/pci.h>
#include <drivers/pci/core/pci_driver.h>

// PCI Identification
#define HDA_PCI_CLASS_CODE          0x04
#define HDA_PCI_SUBCLASS_CODE       0x03
#define HDA_PCI_PROG_IF             0x00

#define HDA_MAX_CODECS              15
#define HDA_STATESTS_CODEC_MASK     0x7FFF
#define HDA_TIMEOUT_MS              100

/*
 * HDA Specification Rev. 1.0a
 * Section 3.3 - High Definition Audio Controller Register Set
 */
#define HDA_REG_GCAP                0x00
#define HDA_REG_VMIN                0x02
#define HDA_REG_VMAJ                0x03
#define HDA_REG_OUTPAY              0x04
#define HDA_REG_INPAY               0x06
#define HDA_REG_GCTL                0x08
#define HDA_REG_WAKEEN              0x0C
#define HDA_REG_STATESTS            0x0E
#define HDA_REG_GSTS                0x10

#define HDA_REG_OUTSTRMPAY          0x18
#define HDA_REG_INSTRMPAY           0x1A

#define HDA_REG_INTCTL              0x20
#define HDA_REG_INTSTS              0x24

#define HDA_REG_WALCLK              0x30
#define HDA_REG_SSYNC               0x38

/*
 * HDA Specification Rev. 1.0a
 * Section 3.3.2, Table 2
 */
#define HDA_GCAP_OSS_SHIFT          12
#define HDA_GCAP_OSS_MASK           (0x0F << HDA_GCAP_OSS_SHIFT)

#define HDA_GCAP_ISS_SHIFT          8
#define HDA_GCAP_ISS_MASK           (0x0F << HDA_GCAP_ISS_SHIFT)

#define HDA_GCAP_BSS_SHIFT          3
#define HDA_GCAP_BSS_MASK           (0x1F << HDA_GCAP_BSS_SHIFT)

#define HDA_GCAP_NSDO_SHIFT         1
#define HDA_GCAP_NSDO_MASK          (0x03 << HDA_GCAP_NSDO_SHIFT)

#define HDA_GCAP_64OK               (1u << 0)

/*
 * HDA Specification Rev. 1.0a
 * Section 3.3.7, Table 7
 */
#define HDA_GCTL_CRST               (1u << 0)

/*
 * HDA Specification Rev. 1.0a
 * Sections 3.3.18 - 3.3.24
 */
#define HDA_REG_CORBLBASE           0x40
#define HDA_REG_CORBUBASE           0x44
#define HDA_REG_CORBWP              0x48
#define HDA_REG_CORBRP              0x4A
#define HDA_REG_CORBCTL             0x4C
#define HDA_REG_CORBSTS             0x4D
#define HDA_REG_CORBSIZE            0x4E

// Section 3.3.22, Table 22
#define HDA_CORBCTL_CMEIE           (1u << 0)
#define HDA_CORBCTL_RUN             (1u << 1)

// Section 3.3.23, Table 23
#define HDA_CORBSTS_CMEI            (1u << 0)

// Section 3.3.21, Table 21
#define HDA_CORBRP_RST              (1u << 15)
#define HDA_CORBRP_PTR_MASK         0x00FF

/*
 * Section 3.3.24, Table 24
 *
 * Upper nibble: supported-size bitmap
 * Lower two bits: selected ring size
 */
#define HDA_CORBSIZE_CAP_SHIFT      4
#define HDA_CORBSIZE_CAP_MASK       0x70
#define HDA_CORBSIZE_SELECT_MASK    0x03

/*
 * HDA Specification Rev. 1.0a
 * Sections 3.3.25 - 3.3.31
 */
#define HDA_REG_RIRBLBASE           0x50
#define HDA_REG_RIRBUBASE           0x54
#define HDA_REG_RIRBWP              0x58
#define HDA_REG_RINTCNT             0x5A
#define HDA_REG_RIRBCTL             0x5C
#define HDA_REG_RIRBSTS             0x5D
#define HDA_REG_RIRBSIZE            0x5E

// Section 3.3.27, Table 27
#define HDA_RIRBWP_RST              (1u << 15)
#define HDA_RIRBWP_PTR_MASK         0x00FF

// Section 3.3.29, Table 29
#define HDA_RIRBCTL_RINTCTL         (1u << 0)
#define HDA_RIRBCTL_DMAEN           (1u << 1)
#define HDA_RIRBCTL_OIC             (1u << 2)

// Section 3.3.30, Table 30
#define HDA_RIRBSTS_RINTFL          (1u << 0)
#define HDA_RIRBSTS_OIS             (1u << 2)

// Section 3.3.31, Table 31
#define HDA_RIRBSIZE_CAP_SHIFT      4
#define HDA_RIRBSIZE_CAP_MASK       0x70
#define HDA_RIRBSIZE_SELECT_MASK    0x03

/*
 * Sections 3.3.24 and 3.3.31
 *
 * These are values written into the low two bits of the
 * CORBSIZE and RIRBSIZE registers.
 */
#define HDA_RING_SIZE_2             0x00
#define HDA_RING_SIZE_16            0x01
#define HDA_RING_SIZE_256           0x02

// These are capability bits AFTER shifting the register's upper nibble right by four
#define HDA_RING_CAP_2              (1u << 0)
#define HDA_RING_CAP_16             (1u << 1)
#define HDA_RING_CAP_256            (1u << 2)

#define HDA_RING_MAX_ENTRIES        256

/*
 * DMA ALLOCATION
  *
 * HDA Specification Rev. 1.0a
 * Section 3.3.18: CORB base address requires a 1 KiB boundary.
 * Section 3.3.25: RIRB base address requires a 2 KiB boundary.
 *
 * Place the RIRB at offset 2048 within the same allocation.
 * Therefore the shared allocation base must be 2 KiB aligned.
 *
 * Maximum CORB size: 1024 bytes.
 * Maximum RIRB size: 2048 bytes.
 */
#define HDA_CORB_DMA_OFFSET         0
#define HDA_RIRB_OFFSET             2048

#define HDA_CORB_MAX_BYTES          1024
#define HDA_RIRB_MAX_BYTES          2048

#define HDA_COMMAND_DMA_SIZE        4096

#define HDA_COMMAND_DMA_ALIGNMENT   2048

/*
 * HDA Specification Rev. 1.0a
 * Section 3.6.5 and Section 4.4.2.1
 */
#define HDA_RIRB_CODEC_MASK         0x0F
#define HDA_RIRB_UNSOL              (1u << 4)

/*
 * HDA Specification Rev. 1.0a
 * Section 7.1 - Codec Verb and Response Structures
 */
#define HDA_VERB_GET_PARAMETER      0xF00
#define HDA_PARAMETER_VENDOR_ID     0x00

/*
 * HDA Specification Rev. 1.0a
 * Section 3.3.35, Table 34
 */
#define HDA_REG_STREAM_DESC_BASE    0x80
#define HDA_STREAM_DESC_STRIDE       0x20
#define HDA_SD_CTL_RUN               (1u << 1)

// Fix stupid CORBRP bug cuz nvidia didn't follow the damn spec...
#define HDA_PCI_VENDOR_NVIDIA       0x10DE

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4 - Parameters
 */

#define HDA_PARAMETER_SUBORDINATE_NODE_COUNT    0x04
#define HDA_PARAMETER_FUNCTION_GROUP_TYPE       0x05
#define HDA_PARAMETER_AUDIO_WIDGET_CAPS         0x09

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.4, Table 137
 */
#define HDA_FUNCTION_GROUP_TYPE_AUDIO           0x01
#define HDA_FUNCTION_GROUP_TYPE_MODEM           0x02

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.6, Table 138
 *
 * Audio Widget Types
 */
#define HDA_WIDGET_TYPE_AUDIO_OUTPUT            0x00
#define HDA_WIDGET_TYPE_AUDIO_INPUT             0x01
#define HDA_WIDGET_TYPE_AUDIO_MIXER              0x02
#define HDA_WIDGET_TYPE_AUDIO_SELECTOR           0x03
#define HDA_WIDGET_TYPE_PIN_COMPLEX              0x04
#define HDA_WIDGET_TYPE_POWER                    0x05
#define HDA_WIDGET_TYPE_VOLUME_KNOB              0x06
#define HDA_WIDGET_TYPE_BEEP_GENERATOR            0x07
#define HDA_WIDGET_TYPE_VENDOR_DEFINED            0x0F

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.6, Figure 86
 *
 * Audio Widget Capability Bits
 */
#define HDA_WIDGET_CAP_STEREO                    (1u << 0)
#define HDA_WIDGET_CAP_IN_AMP                    (1u << 1)
#define HDA_WIDGET_CAP_OUT_AMP                   (1u << 2)
#define HDA_WIDGET_CAP_AMP_OVERRIDE              (1u << 3)
#define HDA_WIDGET_CAP_FORMAT_OVERRIDE           (1u << 4)
#define HDA_WIDGET_CAP_STRIPE                   (1u << 5)
#define HDA_WIDGET_CAP_PROC_WIDGET               (1u << 6)
#define HDA_WIDGET_CAP_UNSOL_CAPABLE             (1u << 7)
#define HDA_WIDGET_CAP_CONN_LIST                 (1u << 8)
#define HDA_WIDGET_CAP_DIGITAL                   (1u << 9)
#define HDA_WIDGET_CAP_POWER_CTRL                (1u << 10)
#define HDA_WIDGET_CAP_LR_SWAP                   (1u << 11)
#define HDA_WIDGET_CAP_CP_CAPS                   (1u << 12)

#define HDA_WIDGET_CAP_TYPE_SHIFT                20
#define HDA_WIDGET_CAP_TYPE_MASK                 (0x0Fu << 20)

#define HDA_NODE_ID_MAX                          0xFF

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.11
 */
#define HDA_PARAMETER_CONNECTION_LIST_LENGTH    0x0E

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.3.3, Table 74
 */
#define HDA_VERB_GET_CONNECTION_LIST_ENTRY      0xF02

/*
 * Connection List Length response
 *
 * Bit 7: Long Form
 * Bits 6:0: Raw connection entry count
 */
#define HDA_CONNECTION_LIST_LONG_FORM           (1u << 7)
#define HDA_CONNECTION_LIST_LENGTH_MASK         0x7F

#define HDA_MAX_CONNECTIONS                     256

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.9
 */
#define HDA_PARAMETER_PIN_CAPABILITIES           0x0C

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.3.31, Table 108
 */
#define HDA_VERB_GET_CONFIG_DEFAULT             0xF1C

/*
 * Pin Capabilities response bits.
 *
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.9, Figure 89
 */
#define HDA_PINCAP_IMPEDANCE_SENSE               (1u << 0)
#define HDA_PINCAP_TRIGGER_REQUIRED              (1u << 1)
#define HDA_PINCAP_PRESENCE_DETECT               (1u << 2)
#define HDA_PINCAP_HEADPHONE_DRIVE               (1u << 3)
#define HDA_PINCAP_OUTPUT                        (1u << 4)
#define HDA_PINCAP_INPUT                         (1u << 5)
#define HDA_PINCAP_BALANCED                      (1u << 6)
#define HDA_PINCAP_HDMI                          (1u << 7)

#define HDA_PINCAP_VREF_SHIFT                    8
#define HDA_PINCAP_VREF_MASK                     (0xFFu << 8)

#define HDA_PINCAP_EAPD                          (1u << 16)
#define HDA_PINCAP_DISPLAYPORT                   (1u << 24)
#define HDA_PINCAP_HBR                           (1u << 27)

#define HDA_MAX_PATH_DEPTH 32
#define HDA_MAX_PATHS_PER_PIN 16
#define HDA_MAX_OUTPUT_PATHS_PER_CODEC 64

// Amplifier capabilities
#define HDA_PARAMETER_INPUT_AMP_CAPS          0x0D
#define HDA_PARAMETER_OUTPUT_AMP_CAPS         0x12

// Power capabilities
#define HDA_PARAMETER_SUPPORTED_POWER_STATES  0x0F

// Get verbs
#define HDA_VERB_GET_POWER_STATE              0xF05
#define HDA_VERB_GET_PIN_WIDGET_CONTROL       0xF07
#define HDA_VERB_GET_EAPD_BTL_ENABLE          0xF0C

#define HDA_VERB_GET_AMP_GAIN_MUTE    0x0B
#define HDA_VERB_SET_AMP_GAIN_MUTE    0x03

#define HDA_VERB_SET_POWER_STATE          0x705
#define HDA_VERB_SET_PIN_WIDGET_CONTROL   0x707
#define HDA_VERB_SET_EAPD_BTL_ENABLE      0x70C

#define HDA_PINCTL_INPUT_ENABLE           (1u << 5)
#define HDA_PINCTL_OUTPUT_ENABLE          (1u << 6)
#define HDA_PINCTL_HEADPHONE_ENABLE       (1u << 7)

#define HDA_EAPD_ENABLE                   (1u << 1)

#define HDA_PARAMETER_SUPPORTED_PCM_SIZE_RATES  0x0A
#define HDA_PARAMETER_SUPPORTED_STREAM_FORMATS  0x0B

// PCM Size/Rate capabilities
#define HDA_PCM_SIZE_8BIT       (1u << 16)
#define HDA_PCM_SIZE_16BIT      (1u << 17)
#define HDA_PCM_SIZE_20BIT      (1u << 18)
#define HDA_PCM_SIZE_24BIT      (1u << 19)
#define HDA_PCM_SIZE_32BIT      (1u << 20)

#define HDA_PCM_RATE_48000      (1u << 6)

// Supported Stream Formats
#define HDA_STREAM_FORMAT_PCM   (1u << 0)

// HDA Stream Format: 48kHz, 16-bit, stereo
#define HDA_FORMAT_48K_16_STEREO  0x0011

#define HDA_PLAYBACK_PAGE_SIZE       4096
#define HDA_PLAYBACK_BUFFER_SIZE     4096
#define HDA_PLAYBACK_BUFFER_COUNT    2

#define HDA_PLAYBACK_STREAM_TAG      1
#define HDA_PLAYBACK_FORMAT          0x0011

#define HDA_VERB_SET_CONVERTER_FORMAT       0x02
#define HDA_VERB_SET_CONVERTER_STREAM       0x706

#define HDA_SD_CTL_SRST                 (1u << 0)

#define HDA_SD_STS_BCIS                 (1u << 2)
#define HDA_SD_STS_FIFOE                (1u << 3)
#define HDA_SD_STS_DESE                 (1u << 4)
#define HDA_SD_STS_FIFORDY              (1u << 5)

#define HDA_SD_FIFOW 0x0E

#define HDA_PIN_DEVICE_LINE_OUT          0x0
#define HDA_PIN_DEVICE_SPEAKER           0x1
#define HDA_PIN_DEVICE_HEADPHONE         0x2

#define HDA_PIN_DEVICE_SPDIF_OUT         0x4
#define HDA_PIN_DEVICE_DIGITAL_OTHER_OUT 0x5

#define HDA_VERB_GET_CONNECTION_SELECT_CONTROL 0xF01
#define HDA_VERB_SET_CONNECTION_SELECT_CONTROL 0x701

typedef struct __packed__ {
    u64 address;
    u32 length;
    u32 flags;
} intel_hda_bdle_t;

STATIC_ASSERT(sizeof(intel_hda_bdle_t) == 16);

/*
 * HDA Specification Rev. 1.0a
 * Sections 3.6.4 and 3.6.5
 */
typedef struct __packed__ {
    u32 command;
} intel_hda_corb_entry_t;

typedef struct __packed__ {
    u32 response;
    u32 extended;
} intel_hda_rirb_entry_t;

STATIC_ASSERT(sizeof(intel_hda_corb_entry_t) == 4);
STATIC_ASSERT(sizeof(intel_hda_rirb_entry_t) == 8);

/*
 * Intel HDA Specification Rev. 1.0a
 * Sections 3.3.35–3.3.43
 * Stream Descriptor register layout
 */
typedef struct __packed__ {
    u8  ctl[3];         // 0x00: SDnCTL
    u8  sts;            // 0x03: SDnSTS
    u32 lpib;           // 0x04: SDnLPIB
    u32 cbl;            // 0x08: SDnCBL
    u16 lvi;            // 0x0C: SDnLVI
    u16 fifow;          // 0x0E: SDnFIFOW
    u16 fifos;          // 0x10: SDnFIFOS
    u16 fmt;            // 0x12: SDnFMT
    u32 reserved1;      // 0x14
    u32 bdpl;           // 0x18: SDnBDPL
    u32 bdpu;           // 0x1C: SDnBDPU
} intel_hda_stream_desc_t;

/*
 *
 * HDA Specification Rev. 1.0a
 * Section 3.3 - Controller Register Set
 *
 * Size: 0x80 bytes before the stream descriptor array.
 */
typedef struct __packed__ {
    u16 gcap;               // 0x00
    u8  vmin;               // 0x02
    u8  vmaj;               // 0x03
    u16 outpay;             // 0x04
    u16 inpay;              // 0x06
    u32 gctl;               // 0x08
    u16 wakeen;             // 0x0C
    u16 statests;           // 0x0E
    u16 gsts;               // 0x10

    u8 reserved0[6];        // 0x12 - 0x17

    u16 outstrmpay;         // 0x18
    u16 instrmpay;          // 0x1A

    u8 reserved1[4];        // 0x1C - 0x1F

    u32 intctl;             // 0x20
    u32 intsts;             // 0x24

    u8 reserved2[8];        // 0x28 - 0x2F

    u32 walclk;             // 0x30

    u8 reserved3[4];        // 0x34 - 0x37

    u32 ssync;              // 0x38

    u8 reserved4[4];        // 0x3C - 0x3F

    /* CORB: 0x40 - 0x4F */
    u32 corblbase;          // 0x40
    u32 corbubase;          // 0x44
    u16 corbwp;             // 0x48
    u16 corbrp;             // 0x4A
    u8 corbctl;             // 0x4C
    u8 corbsts;             // 0x4D
    u8 corbsize;            // 0x4E
    u8 reserved5;           // 0x4F

    /* RIRB: 0x50 - 0x5F */
    u32 rirblbase;          // 0x50
    u32 rirbubase;          // 0x54
    u16 rirbwp;             // 0x58
    u16 rintcnt;            // 0x5A
    u8 rirbctl;             // 0x5C
    u8 rirbsts;             // 0x5D
    u8 rirbsize;            // 0x5E
    u8 reserved6;           // 0x5F

    /* Immediate Command: 0x60 - 0x6F */
    u32 icoi;               // 0x60
    u32 icii;               // 0x64
    u16 icis;               // 0x68
    u8 reserved7[6];        // 0x6A - 0x6F

    /* DMA Position Buffer: 0x70 - 0x7F */
    u32 dmaplbase;          // 0x70
    u32 dmapubase;          // 0x74
    u8 reserved8[8];        // 0x78 - 0x7F

    // stream descriptors begin at 0x80
    intel_hda_stream_desc_t streams[];
} intel_hda_regs_t;

typedef struct {
    b8 valid;

    u8 nid;
    u8 type;

    u32 capabilities;

    u8* connections;
    u16 num_connections;

    u32 pin_capabilities;
    u32 pin_config;

    u32 input_amp_caps;
    u32 output_amp_caps;

    b8 has_pin_caps;
    b8 has_pin_config;

    b8 has_input_amp_caps;
    b8 has_output_amp_caps;

    u8 function_group_nid;
} intel_hda_widget_t;

typedef struct {

    b8 valid;

    // physical endpoint information
    u8 pin_nid;
    u8 dac_nid;

    u8 pin_device;
    u8 association;
    u8 sequence;

    u32 pin_config;

    // number of widgets in this path | nodes are ordered: DAC -> ... -> Pin
    u8 depth;

    u8 nodes[HDA_MAX_PATH_DEPTH];

    /*
     * source_index[i] is the index in: widgets[nodes[i]]->connections[] that refers to nodes[i - 1].
     * Index 0 is unused
     *
     * Examples:
     * Mixer: used as input amplifier index.
     * Selector: used as selected connection index.
     * Pin Complex: used as selected connection index
     *              when multiple sources are available.
     */
    u8 source_index[HDA_MAX_PATH_DEPTH];

    u8 function_group_nid;

    b8 configured;
} intel_hda_output_path_t;

typedef struct {
    INTERFACE_IMPLEMENT(IPCIDRIVER_MEMBERS, IPCIDRIVER_METHODS);

    volatile intel_hda_regs_t* regs;

    u16 codec_mask;
    u8 num_codecs;

    // CORB
    intel_hda_corb_entry_t* corb;
    paddr_t corb_phys;
    u16 corb_entries;
    u16 corb_wp;

    // RIRB
    intel_hda_rirb_entry_t* rirb;
    paddr_t rirb_phys;
    u16 rirb_entries;
    u16 rirb_rp;

    // controller capabilities
    u8 num_iss;
    u8 num_oss;
    u8 num_bss;

    b8 supports_64bit_dma;

    vaddr_t command_dma_virt;
    paddr_t command_dma_phys;
    size_t command_dma_size;

    b8 command_dma_ready;

    // playback DMA memory, each allocation is an independent DMA page.
    vaddr_t playback_bdl_virt;
    paddr_t playback_bdl_phys;
    size_t playback_bdl_alloc_size;

    vaddr_t playback_buffer_virt[HDA_PLAYBACK_BUFFER_COUNT];
    paddr_t playback_buffer_phys[HDA_PLAYBACK_BUFFER_COUNT];
    size_t playback_buffer_alloc_size[HDA_PLAYBACK_BUFFER_COUNT];

    b8 playback_dma_allocated;
    b8 playback_prepared;

    u8 playback_stream_index;
    u8 playback_stream_tag;

    u8 playback_codec_addr;
    u8 playback_dac_nid;

    b8 playback_running;

    /*
     * Codec widget tables.
     * Indexed as: widgets[codec_addr][node_id]
     * Each codec gets a table with 256 possible NIDs.
     */
    intel_hda_widget_t* widgets[HDA_MAX_CODECS];

    // discovered output paths, indexed by codec address.
    intel_hda_output_path_t* output_paths[HDA_MAX_CODECS];

    u16 output_path_count[HDA_MAX_CODECS];

    b8 output_paths_truncated[HDA_MAX_CODECS];

    /*
     * Controller-specific quirks:
     *
     * NVIDIA HDA controllers:
     * CORBRP reset bit clears itself after being asserted.
     */
    b8 corbrp_self_clear;
} intel_hda_driver_t;

STATIC_ASSERT(offsetof(intel_hda_regs_t, gcap) == 0x00);
STATIC_ASSERT(offsetof(intel_hda_regs_t, gctl) == 0x08);
STATIC_ASSERT(offsetof(intel_hda_regs_t, gsts) == 0x10);
STATIC_ASSERT(offsetof(intel_hda_regs_t, intctl) == 0x20);
STATIC_ASSERT(offsetof(intel_hda_regs_t, corblbase) == 0x40);
STATIC_ASSERT(offsetof(intel_hda_regs_t, rirblbase) == 0x50);
STATIC_ASSERT(offsetof(intel_hda_regs_t, icoi) == 0x60);
STATIC_ASSERT(offsetof(intel_hda_regs_t, dmaplbase) == 0x70);
STATIC_ASSERT(offsetof(intel_hda_regs_t, streams) == 0x80);

STATIC_ASSERT(sizeof(intel_hda_stream_desc_t) == 0x20);

b8 intel_hda_driver_init_driver(void* self);
b8 intel_hda_driver_reset_controller(intel_hda_driver_t* driver);
b8 intel_hda_driver_init_command_dma(intel_hda_driver_t* driver);
b8 intel_hda_driver_send_verb(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u16 verb, u8 payload, u32* response);
b8 intel_hda_driver_send_verb4(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u8 verb, u16 payload, u32* response);
b8 intel_hda_driver_enumerate_codec(intel_hda_driver_t* driver, u8 codec_addr);

IPCIDRIVER* intel_hda_driver_factory(PCI_Device* dev);