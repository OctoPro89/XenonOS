#pragma once
#include <drivers/usb/hid/hid_handler.h>

typedef struct hid_mouse_handler hid_mouse_handler_t;

typedef struct hid_mouse_handler {
    INTERFACE_IMPLEMENT(IHIDHANDLER_MEMBERS, IHIDHANDLER_METHODS);

    const usb_hid_field_info_t* x_field;
    const usb_hid_field_info_t* y_field;
    const usb_hid_field_info_t* wheel_field;
    const usb_hid_field_info_t** button_fields;
    u8* prev_buttons;
    u16 button_count;
    u8 report_id;
    b8 ready;
} hid_mouse_handler_t;

hid_mouse_handler_t hid_mouse_handler_create();