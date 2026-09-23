#pragma once
#include <kernel.h>
#include <xlibc/xassert.h>

// USB Standard Descriptor Types
#define USB_DESCRIPTOR_DEVICE                                   0x01
#define USB_DESCRIPTOR_CONFIGURATION                            0x02
#define USB_DESCRIPTOR_STRING                                   0x03
#define USB_DESCRIPTOR_INTERFACE                                0x04
#define USB_DESCRIPTOR_ENDPOINT                                 0x05
#define USB_DESCRIPTOR_DEVICE_QUALIFIER                         0x06
#define USB_DESCRIPTOR_OTHER_SPEED_CONFIGURATION                0x07
#define USB_DESCRIPTOR_INTERFACE_POWER                          0x08
#define USB_DESCRIPTOR_OTG                                      0x09
#define USB_DESCRIPTOR_DEBUG                                    0x0A
#define USB_DESCRIPTOR_INTERFACE_ASSOCIATION                    0x0B
#define USB_DESCRIPTOR_BOS                                      0x0F
#define USB_DESCRIPTOR_DEVICE_CAPABILITY                        0x10
#define USB_DESCRIPTOR_WIRELESS_ENDPOINT_COMPANION              0x11
#define USB_DESCRIPTOR_SUPERSPEED_ENDPOINT_COMPANION            0x30
#define USB_DESCRIPTOR_SUPERSPEEDPLUS_ISO_ENDPOINT_COMPANION    0x31

// HID Class-Specific Descriptor Types
#define USB_DESCRIPTOR_HID                                      0x21
#define USB_DESCRIPTOR_HID_REPORT                               0x22
#define USB_DESCRIPTOR_HID_PHYSICAL_REPORT                      0x23

// Hub Descriptor Types
#define USB_DESCRIPTOR_HUB                                      0x29
#define USB_DESCRIPTOR_SUPERSPEED_HUB                           0x2A

// Billboarding Descriptor Type
#define USB_DESCRIPTOR_BILLBOARD                                0x0D

// Type-C Bridge Descriptor Type
#define USB_DESCRIPTOR_TYPE_C_BRIDGE                            0x0E

// USB Interface Class Codes
#define USB_CLASS_AUDIO                                         0x01
#define USB_CLASS_CDC                                           0x02
#define USB_CLASS_HID                                           0x03
#define USB_CLASS_MASS_STORAGE                                  0x08
#define USB_CLASS_HUB                                           0x09
#define USB_CLASS_VENDOR                                        0xFF

#define USB_DESCRIPTOR_REQUEST(type, index) (u16)(((u16)(type) << 8) | (u16)(index))

/*
 * REFERENCE: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_device_descriptor
 */

typedef struct __packed__ usb_descriptor_header {
    u8 bLength;
    u8 bDescriptorType;
} usb_descriptor_header_t;

STATIC_ASSERT(sizeof(usb_descriptor_header_t) == 2);

typedef struct __packed__ usb_device_descriptor {
    usb_descriptor_header_t header;
    u16 bcdUsb;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer;
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
} usb_device_descriptor_t;

STATIC_ASSERT(sizeof(usb_device_descriptor_t) == 18);

typedef struct __packed__ usb_string_language_descriptor {
    usb_descriptor_header_t header;
    u16 lang_ids[126];
} usb_string_language_descriptor_t;

STATIC_ASSERT(sizeof(usb_string_language_descriptor_t) == 254);

typedef struct __packed__ usb_string_descriptor {
    usb_descriptor_header_t header;
    u16 unicode_string[126];
} usb_string_descriptor_t;

STATIC_ASSERT(sizeof(usb_string_descriptor_t) == 254);

typedef struct __packed__ usb_configuration_descriptor {
    usb_descriptor_header_t header;
    u16 wTotalLength;
    u8 bNumInterfaces;
    u8 bConfigurationValue;
    u8 iConfiguration;
    u8 bmAttributes;
    u8 bMaxPower;
    u8 data[245];
} usb_configuration_descriptor_t;

STATIC_ASSERT(sizeof(usb_configuration_descriptor_t) == 254);

typedef struct __packed__ usb_interface_descriptor {
    usb_descriptor_header_t header;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} usb_interface_descriptor_t;

STATIC_ASSERT(sizeof(usb_interface_descriptor_t) == 9);

typedef struct __packed__ usb_hid_descriptor {
    usb_descriptor_header_t header;
    u16 bcdHID;
    u8  bCountryCode;
    u8  bNumDescriptors;
    struct __packed__ {
        u8  bDescriptorType;
        u16 wDescriptorLength;
    } desc[1];
} usb_hid_descriptor_t;

STATIC_ASSERT(sizeof(usb_hid_descriptor_t) == 9);

typedef struct __packed__ usb_endpoint_descriptor {
    usb_descriptor_header_t header;
    u8 bEndpointAddress;
    u8 bmAttributes;
    u16 wMaxPacketSize;
    u8 bInterval;
} usb_endpoint_descriptor_t;

STATIC_ASSERT(sizeof(usb_endpoint_descriptor_t) == 7);