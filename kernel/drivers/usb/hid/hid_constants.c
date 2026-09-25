#include <drivers/usb/hid/hid_constants.h>

// TODO: CHECK THIS WHOLE FILE

const char* usb_hid_item_type_to_string(usb_hid_item_type_t type) {
    switch (type) {
        case USB_HID_ITEM_TYPE_MAIN:    { return "Main"; }
        case USB_HID_ITEM_TYPE_GLOBAL:  { return "Global"; }
        case USB_HID_ITEM_TYPE_LOCAL:   { return "Local"; }
        default:                        { return "Reserved"; }
    }

    return "Reserved";
}

const char* usb_hid_main_item_tag_to_string(usb_hid_main_item_tag_t tag) {
    switch (tag) {
        case USB_HID_MAIN_ITEM_TAG_INPUT:           { return "Input"; }
        case USB_HID_MAIN_ITEM_TAG_OUTPUT:          { return "Output"; }
        case USB_HID_MAIN_ITEM_TAG_FEATURE:         { return "Feature"; }
        case USB_HID_MAIN_ITEM_TAG_COLLECTION:      { return "Collection"; }
        case USB_HID_MAIN_ITEM_TAG_END_COLLECTION:  { return "End Collection"; }
        default:                                    { return "Unknown"; }
    }

    return "Unknown";
}

const char* usb_hid_global_item_tag_to_string(usb_hid_global_item_tag_t tag) {
    switch (tag) {
        case USB_HID_GLOBAL_ITEM_TAG_USAGE_PAGE:        { return "Usage Page"; }
        case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:   { return "Logical Minimum"; }
        case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:   { return "Logical Maximum"; }
        case USB_HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM:  { return "Physical Minimum"; }
        case USB_HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM:  { return "Physical Maximum"; }
        case USB_HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT:     { return "Unit Exponent"; }
        case USB_HID_GLOBAL_ITEM_TAG_UNIT:              { return "Unit"; }
        case USB_HID_GLOBAL_ITEM_TAG_REPORT_SIZE:       { return "Report Size"; }
        case USB_HID_GLOBAL_ITEM_TAG_REPORT_ID:         { return "Report ID"; }
        case USB_HID_GLOBAL_ITEM_TAG_REPORT_COUNT:      { return "Report Count"; }
        case USB_HID_GLOBAL_ITEM_TAG_PUSH:              { return "Push"; }
        case USB_HID_GLOBAL_ITEM_TAG_POP:               { return "Pop"; }
        default:                                        { return "Unknown"; }
    }

    return "Unknown";
}

const char* usb_hid_local_item_tag_to_string(usb_hid_local_item_tag_t tag) {
    switch (tag) {
        case USB_HID_LOCAL_ITEM_TAG_USAGE:              { return "Usage"; }
        case USB_HID_LOCAL_ITEM_TAG_USAGE_MINIMUM:      { return "Usage Minimum"; }
        case USB_HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM:      { return "Usage Maximum"; }
        case USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX:   { return "Designator Index"; }
        case USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM: { return "Designator Minimum"; }
        case USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM: { return "Designator Maximum"; }
        case USB_HID_LOCAL_ITEM_TAG_STRING_INDEX:       { return "String Index"; }
        case USB_HID_LOCAL_ITEM_TAG_STRING_MINIMUM:     { return "String Minimum"; }
        case USB_HID_LOCAL_ITEM_TAG_STRING_MAXIMUM:     { return "String Maximum"; }
        case USB_HID_LOCAL_ITEM_TAG_DELIMITER:          { return "Delimiter"; }
        default:                                        { return "Unknown"; }
    }

    return "Unknown";
}