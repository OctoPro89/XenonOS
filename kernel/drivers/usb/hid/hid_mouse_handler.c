#include <drivers/usb/hid/hid_mouse_handler.h>
#include <drivers/usb/hid/hid_constants.h>
#include <drivers/input/input.h>
#include <xlibc/xassert.h>
#include <xlibc/xstdint.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <xlibc/stdio.h>

static void hid_mouse_handler_reset_state(hid_mouse_handler_t* handler) {
    if (handler->button_fields) {
        kfree(handler->button_fields);
        handler->button_fields = NULL;
    }

    if (handler->prev_buttons) {
        kfree(handler->prev_buttons);
        handler->prev_buttons = NULL;
    }

    handler->x_field = NULL;
    handler->y_field = NULL;
    handler->wheel_field = NULL;
    handler->button_count = 0;
    handler->report_id = 0;
    handler->ready = false;
}

b8 hid_mouse_handler_init(void* _self, const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report) {
    hid_mouse_handler_t* self = (hid_mouse_handler_t*)_self;
    hid_mouse_handler_reset_state(self);

    u16 gd = (u16)USB_HID_USAGE_PAGE_GENERIC_DESKTOP;
    u16 btn = (u16)USB_HID_USAGE_PAGE_BUTTONS;
    const usb_hid_field_info_t* fields = usb_hid_report_fields(layout, report);
    if (!fields || report->field_count == 0) {
        xassert(false, "");
        return false;
    }

    u16 button_count = 0;
    for (u16 i = 0; i < report->field_count; ++i) {
        const usb_hid_field_info_t* field = &fields[i];
        if (USB_HID_FIELD_INFO_IS_CONSTANT(*field)) {
            continue;
        }

        if (!self->x_field && field->usage_page == gd && field->usage == (u16)USB_HID_GENERIC_DESKTOP_USAGE_X_AXIS) {
            self->x_field = field;
            continue;
        }

        if (!self->y_field && field->usage_page == gd && field->usage == (u16)USB_HID_GENERIC_DESKTOP_USAGE_Y_AXIS) {
            self->y_field = field;
            continue;
        }

        if (!self->wheel_field && field->usage_page == gd && field->usage == (u16)USB_HID_GENERIC_DESKTOP_USAGE_WHEEL) {
            self->wheel_field = field;
            continue;
        }

        if (field->usage_page == btn && USB_HID_FIELD_INFO_IS_VARIABLE(*field)) {
            ++button_count;
        }
    }

    if (!self->x_field || !self->y_field) {
        hid_mouse_handler_reset_state(self);
        return false;
    }

    if (button_count > 0) {
        self->button_fields = (const usb_hid_field_info_t**)kmalloc(button_count * sizeof(usb_hid_field_info_t*));
        self->prev_buttons = (u8*)kmalloc(button_count * sizeof(u8));
        if (!self->button_fields || !self->prev_buttons) {
            hid_mouse_handler_reset_state(self);
            return false;
        }

        u16 button_index = 0;
        for (u16 i = 0; i < report->field_count; ++i) {
            const usb_hid_field_info_t* field = &fields[i];
            if (!USB_HID_FIELD_INFO_IS_CONSTANT(*field) && field->usage_page == btn && USB_HID_FIELD_INFO_IS_VARIABLE(*field)) {
                self->button_fields[button_index++] = field;
            }
        }

        memset(self->prev_buttons, 0, button_count * sizeof(u8));
    }

    self->button_count = button_count;
    self->report_id = report->report_id;
    self->ready = true;

    printf("[HID MOUSE]: report=%u buttons=%u wheel=%s\n", self->report_id, self->button_count, self->wheel_field ? "yes" : "no");

    return true;
}

void hid_mouse_handler_on_report(void* _self, const u8* data, u32 length) {
    hid_mouse_handler_t* self = (hid_mouse_handler_t*)_self;
    if (!self->ready) { return; }

    i32 dx = 0;
    i32 dy = 0;
    i32 scroll = 0;

    if (self->x_field) {
        dx = (self->x_field->logical_minimum < 0 || USB_HID_FIELD_INFO_IS_RELATIVE(*self->x_field))
            ? usb_hid_read_field_signed(data, length, self->x_field->bit_offset, self->x_field->bit_size)
            : (i32)(usb_hid_read_field_unsigned(data, length, self->x_field->bit_offset, self->x_field->bit_size));
    }

    if (self->y_field) {
        dy = (self->y_field->logical_minimum < 0 || USB_HID_FIELD_INFO_IS_RELATIVE(*self->y_field))
            ? usb_hid_read_field_signed(data, length, self->y_field->bit_offset, self->y_field->bit_size)
            : (i32)(usb_hid_read_field_unsigned(data, length, self->y_field->bit_offset, self->y_field->bit_size));
    }

    if (self->wheel_field) {
        scroll = (self->wheel_field->logical_minimum < 0 || USB_HID_FIELD_INFO_IS_RELATIVE(*self->wheel_field))
            ? usb_hid_read_field_signed(data, length, self->wheel_field->bit_offset, self->wheel_field->bit_size)
            : (i32)usb_hid_read_field_unsigned(data, length, self->wheel_field->bit_offset, self->wheel_field->bit_size);
    }

    u16 buttons = 0;
    b8 buttons_changed = false;
    for (u16 i = 0; i < self->button_count; ++i) {
        const usb_hid_field_info_t* field = self->button_fields[i];
        b8 pressed = usb_hid_read_field_unsigned(data, length, field->bit_offset, field->bit_size);
        if (pressed) {
            buttons |= (u16)(1u << i);
        }

        b8 was_pressed = self->prev_buttons[i] != 0;
        if (pressed != was_pressed) {
            buttons_changed = true;
        }

        self->prev_buttons[i] = pressed ? 1 : 0;
    }

    b8 is_relative = self->x_field && (self->x_field->logical_minimum < 0 || USB_HID_FIELD_INFO_IS_RELATIVE(*self->x_field));

    if (dx != 0 || dy != 0 || scroll != 0 || buttons_changed) {
        input_mouse_event_t evt;
        evt.x_value = dx;
        evt.y_value = dy;
        evt.wheel = (i16)scroll;
        evt.buttons = buttons;
        evt.flags = is_relative ? INPUT_MOUSE_FLAG_RELATIVE : 0;
        input_push_mouse_event(&evt);
    }
}

void hid_mouse_handler_destroy(void* _self) {
    hid_mouse_handler_t* self = (hid_mouse_handler_t*)_self;
    hid_mouse_handler_reset_state(self);
}

hid_mouse_handler_t hid_mouse_handler_create() {
    hid_mouse_handler_t handler;
    memset(&handler, 0, sizeof(hid_mouse_handler_t));
    handler.init = hid_mouse_handler_init;
    handler.on_report = hid_mouse_handler_on_report;
    handler.destroy = hid_mouse_handler_destroy;
    
    return handler;
}