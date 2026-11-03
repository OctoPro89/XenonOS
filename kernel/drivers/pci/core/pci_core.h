/**
 * XenonOS PCI Driver Core
 * ***IMPORTANT*** Not thread-safe because PCI Core is intended to be setup before other threads are spawned etc. 
 */

#pragma once

#include <drivers/pci/core/pci_driver.h>

/**
 * @brief Loads device into PCI Core
 * @note Should be called by PCI driver in pci_scan()
 */
void pci_core_device_configured(PCI_Device* device);

/**
 * @brief Registers a driver into the PCI Core
 * @note Also creates a task for the driver
 * 
 * @param driver Should be a pointer to a heap allocated struct extending the IPCIDRIVER interface
 * @param match Matching structure for assigning driver
 * @param factory Function called to create and manage driver
 */
void pci_core_register_driver(const char* name, pci_core_interface_match_t match, pci_core_interface_driver_factory_func factory);