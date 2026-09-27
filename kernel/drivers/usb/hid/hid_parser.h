#pragma once
#include <drivers/usb/hid/hid_constants.h>
#include <xlibc/xstddef.h>

// TODO: CHECK THIS WHOLE FILE
// TODO: consider static inline functions

typedef struct {
    usb_hid_item_type_t type;
    u8 tag;
    u8 size;    // data payload size in bytes (0, 1, 2, or 4)
    u32 data;   // data payload (little-endian, zero-extended)
} usb_hid_report_item_t;

typedef u16 usb_hid_input_field_flags;

#define USB_HID_INPUT_FIELD_CONSTANT ((u16)(1u << 0))
#define USB_HID_INPUT_FIELD_VARIABLE ((u16)(1u << 1))
#define USB_HID_INPUT_FIELD_RELATIVE ((u16)(1u << 2))

// a parsed field within one input report body, bit_offset is relative to the
// start of the report body, not including any leading report ID byte on wire
typedef struct {
    u32 bit_offset;
    u16 bit_size;
    u8 report_id;
    u16 usage_page;
    u16 usage;
    u16 input_flags;
    i32 logical_minimum;
    i32 logical_maximum;
} usb_hid_field_info_t;

#define USB_HID_FIELD_INFO_IS_CONSTANT(x) ((b8)(((x).input_flags & USB_HID_INPUT_FIELD_CONSTANT) != 0))
#define USB_HID_FIELD_INFO_IS_VARIABLE(x) ((b8)(((x).input_flags & USB_HID_INPUT_FIELD_VARIABLE) != 0))
#define USB_HID_FIELD_INFO_IS_RELATIVE(x) ((b8)(((x).input_flags & USB_HID_INPUT_FIELD_RELATIVE) != 0))

typedef struct {
    u8 report_id;
    u32 byte_length; // report body length, excluding any report ID byte
    u16 field_begin;
    u16 field_count;
} usb_hid_input_report_info_t;

// parsed input-report layout from an HID report descriptor
// owns heap-allocated arrays, call destroy() when done
typedef struct {
    usb_hid_field_info_t* fields;
    u16 num_fields;
    usb_hid_input_report_info_t* input_reports;
    u16 num_input_reports;
    b8 uses_report_ids;
    u32 max_input_report_bytes;
} usb_hid_report_layout_t;

void usb_hid_report_layout_destroy(usb_hid_report_layout_t* layout);

const usb_hid_input_report_info_t* usb_hid_find_input_report(const usb_hid_report_layout_t* layout, u8 report_id);
const usb_hid_field_info_t* usb_hid_report_fields(const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report);

/**
 * Parse a raw HID report descriptor into a usb_hid_report_layout_t.
 * @note The layout's arrays are heap-allocated, the called must call usb_hid_report_layout_destroy on layout
 */
b8 usb_hid_parse_report_descriptor(const u8* descriptor, size_t length, usb_hid_report_layout_t* out);

// bitfield extraction utilities for reading values from HID report bodies
u32 usb_hid_read_field_unsigned(const u8* data, u32 length, u32 bit_offset, u16 bit_size);
i32 usb_hid_read_field_signed(const u8* data, u32 length, u32 bit_offset, u16 bit_size);