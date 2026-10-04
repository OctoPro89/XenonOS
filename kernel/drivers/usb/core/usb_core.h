#pragma once
#include <drivers/usb/usb_descriptors.h>
#include <drivers/usb/core/usb_driver.h>
#include <drivers/usb/xhci.h>

// TODO: comments
void usb_core_device_configured(xhci_driver_t* driver, xhci_device_t* xdev, const usb_device_descriptor_t* desc);
void usb_core_device_disconnected(xhci_driver_t* driver, xhci_device_t* xdev);
void usb_core_device_finalized_disconnected_device(xhci_driver_t* driver, xhci_device_t* xdev);

/**
 * @brief Registers a driver into the USB Core
 * @note Also creates a task for the driver
 * 
 * @param driver Should be a pointer to a heap allocated struct extending the IUSBDRIVER interface
 * @param match Matching structure for assigning driver
 * @param factory Function called to create and manage driver
 */
void usb_core_register_driver(const char* name, usb_core_interface_match_t match, usb_core_interface_driver_factory_func factory);