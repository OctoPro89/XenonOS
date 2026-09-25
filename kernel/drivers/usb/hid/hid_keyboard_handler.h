#pragma once
#include <drivers/usb/hid/hid_handler.h>
#include <drivers/usb/core/usb_driver.h>
#include <kernel.h>

// TODO:

static __hint_inline__ b8 hid_keyboard_handler_is_modifier_usage(u16 usage) {
    return usage >= 0xE0u && usage <= 0xE7u;
}

static __hint_inline__ b8 hid_keyboard_handler_is_reserved_array_usage(u16 usage) {
    return usage >= 0x01u && usage <= 0x03u;
}

typedef struct {
    INTERFACE_IMPLEMENT(IUSBDRIVER, IUSBDRIVER_MEMBERS, IUSBDRIVER_METHODS);

    const usb_hid_field_info_t* modifier_fields[8];
    const usb_hid_field_info_t** key_fields;
    u16* prev_keycodes;
    u16* crnt_keycodes;
    u16 key_field_count;
    u8 prev_modifiers;
    u8 report_id;
    b8 ready;
} hid_keyboard_handler_t;