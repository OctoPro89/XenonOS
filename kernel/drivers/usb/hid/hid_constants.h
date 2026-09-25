#pragma once
#include <xlibc/xstdint.h>

// TODO: CHECK THIS WHOLE FILE

typedef u8 usb_hid_item_type_t;

#define USB_HID_ITEM_TYPE_MAIN                                  ((u8)0x0)     // main item: input / output / feature items
#define USB_HID_ITEM_TYPE_GLOBAL                                ((u8)0x1)     // global item: report size, usage page, etc.
#define USB_HID_ITEM_TYPE_LOCAL                                 ((u8)0x2)     // local item: usage, usage minimum / maximum
#define USB_HID_ITEM_TYPE_RESERVED                              ((u8)0x3)     // reserved type

typedef u8 usb_hid_main_item_tag_t;

#define USB_HID_MAIN_ITEM_TAG_INPUT                             ((u8)0x8)     // input item
#define USB_HID_MAIN_ITEM_TAG_OUTPUT                            ((u8)0x9)     // output item
#define USB_HID_MAIN_ITEM_TAG_FEATURE                           ((u8)0xB)     // feature item
#define USB_HID_MAIN_ITEM_TAG_COLLECTION                        ((u8)0xA)     // start a collection (application, physical, etc.)
#define USB_HID_MAIN_ITEM_TAG_END_COLLECTION                    ((u8)0xC)     // end a collection

typedef u8 usb_hid_global_item_tag_t;

#define USB_HID_GLOBAL_ITEM_TAG_USAGE_PAGE                      ((u8)0x0)     // usage page (what category of input)
#define USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM                 ((u8)0x1)     // minimum logical value
#define USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM                 ((u8)0x2)     // maximum logical value
#define USB_HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM                ((u8)0x3)     // minimum physical value
#define USB_HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM                ((u8)0x4)     // maximum physical value
#define USB_HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT                   ((u8)0x5)     // unit exponent
#define USB_HID_GLOBAL_ITEM_TAG_UNIT                            ((u8)0x6)     // unit system (SI units)
#define USB_HID_GLOBAL_ITEM_TAG_REPORT_SIZE                     ((u8)0x7)     // number of bits for each data field
#define USB_HID_GLOBAL_ITEM_TAG_REPORT_ID                       ((u8)0x8)     // report id (used for multi-report devices)
#define USB_HID_GLOBAL_ITEM_TAG_REPORT_COUNT                    ((u8)0x9)     // number of fields in the report
#define USB_HID_GLOBAL_ITEM_TAG_PUSH                            ((u8)0xA)     // push context onto stack
#define USB_HID_GLOBAL_ITEM_TAG_POP                             ((u8)0xB)     // pop context from stack

typedef u8 usb_hid_local_item_tag_t;

#define USB_HID_LOCAL_ITEM_TAG_USAGE                            ((u8)0x0)     // specific usage within a usage page (e.g., x-axis, button)
#define USB_HID_LOCAL_ITEM_TAG_USAGE_MINIMUM                    ((u8)0x1)     // minimum usage value
#define USB_HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM                    ((u8)0x2)     // maximum usage value
#define USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX                 ((u8)0x3)     // designator index (optional physical index)
#define USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM               ((u8)0x4)     // minimum designator index
#define USB_HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM               ((u8)0x5)     // maximum designator index
#define USB_HID_LOCAL_ITEM_TAG_STRING_INDEX                     ((u8)0x7)     // string index (optional string descriptor)
#define USB_HID_LOCAL_ITEM_TAG_STRING_MINIMUM                   ((u8)0x8)     // minimum string index
#define USB_HID_LOCAL_ITEM_TAG_STRING_MAXIMUM                   ((u8)0x9)     // maximum string index
#define USB_HID_LOCAL_ITEM_TAG_DELIMITER                        ((u8)0xA)     // used to delimit items in compound reports

// common usage pages and usages
typedef u8 usb_hid_usage_page_t;

#define USB_HID_USAGE_PAGE_GENERIC_DESKTOP                      ((u8)0x1)     // generic desktop controls (e.g., mouse, keyboard, joystick)
#define USB_HID_USAGE_PAGE_SIMULATION                           ((u8)0x2)     // simulation controls (e.g., flight controls)
#define USB_HID_USAGE_PAGE_VR_CONTROLS                          ((u8)0x3)     // virtual reality controls
#define USB_HID_USAGE_PAGE_SPORT_CONTROLS                       ((u8)0x4)     // sports controls
#define USB_HID_USAGE_PAGE_GAME_CONTROLS                        ((u8)0x5)     // game controls
#define USB_HID_USAGE_PAGE_GENERIC_DEVICE                       ((u8)0x6)     // generic device controls
#define USB_HID_USAGE_PAGE_KEYBOARD                             ((u8)0x7)     // keyboard/keypad
#define USB_HID_USAGE_PAGE_LEDS                                 ((u8)0x8)     // led indicators
#define USB_HID_USAGE_PAGE_BUTTONS                              ((u8)0x9)     // button inputs
#define USB_HID_USAGE_PAGE_ORDINAL                              ((u8)0xA)     // ordinal (device index tracking)
#define USB_HID_USAGE_PAGE_TELEPHONY                            ((u8)0xB)     // telephony devices
#define USB_HID_USAGE_PAGE_CONSUMER                             ((u8)0xC)     // consumer controls (e.g., multimedia keys)
#define USB_HID_USAGE_PAGE_DIGITIZER                            ((u8)0xD)     // digitizers (e.g., touch screens)

typedef u8 usb_hid_generic_desktop_usage_t;

// common usages within the generic desktop usage page
#define USB_HID_GENERIC_DESKTOP_USAGE_POINTER                   ((u8)0x01)    // pointer (e.g., mouse pointer)
#define USB_HID_GENERIC_DESKTOP_USAGE_MOUSE                     ((u8)0x02)    // mouse
#define USB_HID_GENERIC_DESKTOP_USAGE_JOYSTICK                  ((u8)0x04)    // joystick
#define USB_HID_GENERIC_DESKTOP_USAGE_GAMEPAD                   ((u8)0x05)    // gamepad
#define USB_HID_GENERIC_DESKTOP_USAGE_KEYBOARD                  ((u8)0x06)    // keyboard
#define USB_HID_GENERIC_DESKTOP_USAGE_KEYPAD                    ((u8)0x07)    // keypad
#define USB_HID_GENERIC_DESKTOP_USAGE_MULTI_AXIS_CONTROLLER     ((u8)0x08)    // multi-axis controller
#define USB_HID_GENERIC_DESKTOP_USAGE_X_AXIS                    ((u8)0x30)    // x-axis movement
#define USB_HID_GENERIC_DESKTOP_USAGE_Y_AXIS                    ((u8)0x31)    // y-axis movement
#define USB_HID_GENERIC_DESKTOP_USAGE_Z_AXIS                    ((u8)0x32)    // z-axis movement
#define USB_HID_GENERIC_DESKTOP_USAGE_WHEEL                     ((u8)0x38)    // scroll wheel
#define USB_HID_GENERIC_DESKTOP_USAGE_HAT_SWITCH                ((u8)0x39)    // hat switch (d-pad)

// HID report descriptor utilities
const char* usb_hid_item_type_to_string(usb_hid_item_type_t type);
const char* usb_hid_main_item_tag_to_string(usb_hid_main_item_tag_t tag);
const char* usb_hid_global_item_tag_to_string(usb_hid_global_item_tag_t tag);
const char* usb_hid_local_item_tag_to_string(usb_hid_local_item_tag_t tag);