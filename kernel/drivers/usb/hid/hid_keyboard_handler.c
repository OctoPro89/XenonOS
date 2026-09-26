#include <drivers/usb/hid/hid_keyboard_handler.h>
#include <drivers/usb/hid/hid_constants.h>
#include <drivers/input/input.h>
#include <xlibc/xstdint.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <xlibc/stdio.h>

// USB HID scancode to key name (US layout)
static const char* scancode_to_name_lookup(uint8_t sc) {
    switch (sc) {
        case 0x04: return "A";     case 0x05: return "B";
        case 0x06: return "C";     case 0x07: return "D";
        case 0x08: return "E";     case 0x09: return "F";
        case 0x0A: return "G";     case 0x0B: return "H";
        case 0x0C: return "I";     case 0x0D: return "J";
        case 0x0E: return "K";     case 0x0F: return "L";
        case 0x10: return "M";     case 0x11: return "N";
        case 0x12: return "O";     case 0x13: return "P";
        case 0x14: return "Q";     case 0x15: return "R";
        case 0x16: return "S";     case 0x17: return "T";
        case 0x18: return "U";     case 0x19: return "V";
        case 0x1A: return "W";     case 0x1B: return "X";
        case 0x1C: return "Y";     case 0x1D: return "Z";
        case 0x1E: return "1";     case 0x1F: return "2";
        case 0x20: return "3";     case 0x21: return "4";
        case 0x22: return "5";     case 0x23: return "6";
        case 0x24: return "7";     case 0x25: return "8";
        case 0x26: return "9";     case 0x27: return "0";
        case 0x28: return "ENTER";     case 0x29: return "ESC";
        case 0x2A: return "BACKSPACE"; case 0x2B: return "TAB";
        case 0x2C: return "SPACE";     case 0x2D: return "MINUS";
        case 0x2E: return "EQUAL";     case 0x2F: return "LBRACKET";
        case 0x30: return "RBRACKET";  case 0x31: return "BACKSLASH";
        case 0x33: return "SEMICOLON"; case 0x34: return "APOSTROPHE";
        case 0x35: return "GRAVE";     case 0x36: return "COMMA";
        case 0x37: return "DOT";       case 0x38: return "SLASH";
        case 0x39: return "CAPSLOCK";
        case 0x3A: return "F1";  case 0x3B: return "F2";
        case 0x3C: return "F3";  case 0x3D: return "F4";
        case 0x3E: return "F5";  case 0x3F: return "F6";
        case 0x40: return "F7";  case 0x41: return "F8";
        case 0x42: return "F9";  case 0x43: return "F10";
        case 0x44: return "F11"; case 0x45: return "F12";
        case 0x49: return "INSERT";  case 0x4A: return "HOME";
        case 0x4B: return "PGUP";   case 0x4C: return "DELETE";
        case 0x4D: return "END";    case 0x4E: return "PGDN";
        case 0x4F: return "RIGHT";  case 0x50: return "LEFT";
        case 0x51: return "DOWN";   case 0x52: return "UP";
        default: return NULL;
    }
}

static void hid_keyboard_handler_reset_state(hid_keyboard_handler_t* handler) {
    if (handler->key_fields) {
        kfree(handler->key_fields);
        handler->key_fields = NULL;
    }

    if (handler->prev_keycodes) {
        kfree(handler->prev_keycodes);
        handler->prev_keycodes = NULL;
    }

    if (handler->crnt_keycodes) {
        kfree(handler->crnt_keycodes);
        handler->crnt_keycodes = NULL;
    }

    for (u8 i = 0; i < 8; ++i) {
        handler->modifier_fields[i] = NULL;
    }

    handler->key_field_count = 0;
    handler->prev_modifiers = 0;
    handler->report_id = 0;
    handler->ready = false;
}

static const char* hid_keyboard_handler_scancode_to_name(u16 scancode) {
        if (scancode > 0xFFu) {
        return NULL;
    }

    return scancode_to_name_lookup((u8)(scancode));
}

static b8 hid_keyboard_handler_contains_keycode(const u16* keycodes, u16 count, u16 keycode) {
    if (!keycodes || keycode == 0) {
        return false;
    }

    for (u16 i = 0; i < count; ++i) {
        if (keycodes[i] == keycode) {
            return true;
        }
    }

    return false;
}

b8 hid_keyboard_handler_init(hid_keyboard_handler_t* handler, const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report) {
    hid_keyboard_handler_reset_state(handler);

    u16 kb = (u16)USB_HID_USAGE_PAGE_KEYBOARD;
    const usb_hid_field_info_t* fields = usb_hid_core_report_fields(layout, report);
    if (!fields || report->field_count == 0) {
        xassert(false, "");
        return false;
    }

    u16 key_field_count = 0;
    for (u16 i = 0; i < report->field_count; ++i) {
        const usb_hid_field_info_t* field = &fields[i];
        if (field->usage_page != kb || USB_HID_FIELD_INFO_IS_CONSTANT(*field)) {
            continue;
        }

        if (USB_HID_FIELD_INFO_IS_VARIABLE(*field) && hid_keyboard_handler_is_modifier_usage(field->usage)) {
            if (field->usage >= 0xE0 && field->usage <= 0xE7) {
                if (!handler->modifier_fields[field->usage - 0xE0]) {
                    handler->modifier_fields[field->usage - 0xE0] = &field;
                }
            }
            continue;
        }

        ++key_field_count;
    }

    if (key_field_count == 0) {
        hid_keyboard_handler_reset_state(handler);
        return false;
    }

    handler->key_fields = (const usb_hid_field_info_t**)kmalloc(key_field_count * sizeof(usb_hid_field_info_t*));
    handler->prev_keycodes = (u16*)kmalloc(key_field_count * sizeof(u16));
    handler->crnt_keycodes = (u16*)kmalloc(key_field_count * sizeof(u16));

    if (!handler->key_fields || !handler->prev_keycodes || !handler->crnt_keycodes) {
        hid_keyboard_handler_reset_state(handler);
        return false;
    }

    u16 slot = 0;
    for (u16 i = 0; i < report->field_count; ++i) {
        const usb_hid_field_info_t* field = &fields[i];
        if (field->usage_page != kb || USB_HID_FIELD_INFO_IS_CONSTANT(*field)) {
            continue;
        }

        if (USB_HID_FIELD_INFO_IS_VARIABLE(*field) && hid_keyboard_handler_is_modifier_usage(field->usage)) {
            continue;
        }

        if (slot < key_field_count) {
            handler->key_fields[slot++] = &field;
        }
    }

    memset(handler->prev_keycodes, 0, key_field_count * sizeof(u16));
    memset(handler->crnt_keycodes, 0, key_field_count * sizeof(u16));

    handler->key_field_count = key_field_count;
    handler->report_id = report->report_id;
    handler->ready = true;

    u8 modifier_count = 0;
    for (u8 i = 0; i < 8; ++i) {
        if (handler->modifier_fields[i]) {
            ++modifier_count;
        }
    }

    printf("[HID-KBD]: report=%u modifiers=%u key-fields=%u\n", handler->report_id, modifier_count, handler->key_field_count);

    return true;
}

void hid_keyboard_handler_on_report(hid_keyboard_handler_t* self, const u8* data, u32 length) {
    if (!self->ready) { return; }

    u8 modifiers = 0;
    for (u8 i = 0; i < 8; ++i) {
        const usb_hid_field_info_t* field = self->modifier_fields[i];
        if (!field) { continue; }

        if (usb_hid_read_field_unsigned(data, length, field->bit_offset, field->bit_size) != 0) {
            modifiers |= (u8)(1u << i);
        }
    }

    u8 mod_changed = modifiers ^ self->prev_modifiers;
    if (mod_changed) {
        for (i32 b = 0; b < 8; ++b) {
            if ((mod_changed & (1u << b)) != 0) {
                b8 pressed = (modifiers & (1u << b)) != 0;
                input_keyboard_event_t mevt;
                mevt.action = pressed ? INPUT_KBD_ACTION_DOWN : INPUT_KBD_ACTION_UP;
                mevt.modifiers = modifiers;
                mevt.usage = (u16)(0xE0u + b);
                input_push_keyboard_event(mevt);
            }
        }

        self->prev_modifiers = modifiers;
    }

    for (u16 i = 0; i < self->key_field_count; ++i) {
        const usb_hid_field_info_t* field = self->key_fields[i];
        u32 raw = usb_hid_read_field_unsigned(data, length, field->bit_offset, field->bit_size);
        u16 keycode = 0;

        if (USB_HID_FIELD_INFO_IS_VARIABLE(*field)) {
            keycode = raw != 0 ? field->usage : 0;
        } else if (raw <= 0xFFFFu) {
            keycode = (uint16_t)(raw);
            if (is_reserved_array_usage(keycode)) {
                keycode = 0;
            }
        }

        self->crnt_keycodes[i] = keycode;
    }

    for (u16 i = 0; i < self->key_field_count; ++i) {
        u16 keycode = self->crnt_keycodes[i];
                if (keycode == 0 || contains_keycode(self->crnt_keycodes, i, keycode)) {
            continue;
        }

        if (!contains_keycode(self->prev_keycodes, self->key_field_count, keycode)) {
            input_kbd_event_t evt;
            evt.action = INPUT_KBD_ACTION_DOWN;
            evt.modifiers = modifiers;
            evt.usage = keycode;
            input_push_kbd_event(evt);
        }
    }

    for (u16 i = 0; i < self->key_field_count; ++i) {
        u16 keycode = self->prev_keycodes[i];
        if (keycode == 0 || contains_keycode(self->prev_keycodes, i, keycode)) {
            continue;
        }

        if (!contains_keycode(self->crnt_keycodes, self->key_field_count, keycode)) {
            input_kbd_event evt;
            evt.action = INPUT_KBD_ACTION_UP;
            evt.modifiers = modifiers;
            evt.usage = keycode;
            input_push_kbd_event(evt);
        }
    }

    memcpy(self->prev_keycodes, self->crnt_keycodes, self->key_field_count * sizeof(u16));
}

void hid_keyboard_handler_destroy(void* self) {
    hid_keyboard_handler_reset_state((hid_keyboard_handler_t*)self);
}

hid_keyboard_handler_t hid_keyboard_handler_create() {
    hid_keyboard_handler_t handler;
    memset(&handler, 0, sizeof(hid_keyboard_handler_t));
    handler.init = hid_keyboard_handler_init;
    handler.on_report = hid_keyboard_handler_on_report;
    handler.destroy = hid_keyboard_handler_destroy;
    
    return handler;
}