#pragma once

#include <xlibc/xinterface.h>
#include <xlibc/xstdint.h>
#include <drivers/pci/pci.h>

#define PCI_MATCH_ANY ((u8)0xFF)

#define PCI_MAKE_MATCH(_class_code, _subclass, _prog_if) (pci_core_interface_match_t){ .class_code = _class_code, .subclass = _subclass, .prog_if = _prog_if }

typedef struct {
    u8 class_code; // PCI_MATCH_ANY = wildcard
    u8 subclass;   // PCI_MATCH_ANY = wildcard
    u8 prog_if;    // PCI_MATCH_ANY = wildcard
} pci_core_interface_match_t;

typedef struct task task_t;

#define IPCIDRIVER_MEMBERS(x) \
    x(const char*, name) \
    x(task_t*, task) \
    x(PCI_Device*, pci_device)

#define IPCIDRIVER_METHODS(x) \
    x(b8, init, (void*)) \
    x(void, run, (void*))

INTERFACE(IPCIDRIVER, IPCIDRIVER_MEMBERS, IPCIDRIVER_METHODS);

typedef IPCIDRIVER* (*pci_core_interface_driver_factory_func)(PCI_Device* dev);