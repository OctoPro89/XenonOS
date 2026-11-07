#include <drivers/pci/core/pci_core.h>
#include <xlibc/xassert.h>
#include <xlibc/stdio.h>
#include <memory/heap.h>
#include <task.h>

#define MAX_PCI_DEVICES 16
#define MAX_PCI_DRIVERS 16

typedef struct {
    pci_core_interface_match_t match;
    pci_core_interface_driver_factory_func create;
    const char* name;
} pci_core_interface_driver_entry_t;

typedef struct {
    PCI_Device* device;
    IPCIDRIVER* driver;
} pci_core_binding_t;

static pci_core_binding_t g_bindings[PCI_MAX_DEVICES];
static u16 g_binding_count;

static pci_core_interface_driver_entry_t g_registered_drivers[MAX_PCI_DRIVERS];
static u16 g_registered_driver_count;

static b8 match_interface(const pci_core_interface_match_t match, const PCI_Device* dev) {
    if (match.class_code != PCI_MATCH_ANY && match.class_code != dev->class_code) {
        return false;
    }

    if (match.subclass != PCI_MATCH_ANY && match.subclass != dev->subclass) {
        return false;
    }

    if (match.prog_if != PCI_MATCH_ANY && match.prog_if != dev->prog_if) {
        return false;
    }

    return true;
}

static void class_driver_task_entry(void* arg) {
    IPCIDRIVER* drv = (IPCIDRIVER*)arg;
    drv->run(drv);

    // NOTE: Realistically not sure if this will ever happen but if it does ig free driver?
    // TODO: device cleanup?
    kfree(drv);
    
    task_exit();
}

static pci_core_binding_t* pci_core_reserve_device(PCI_Device* device) {
    for (u16 i = 0; i < g_binding_count; i++) {
        pci_core_binding_t* binding = &g_bindings[i];

        if (binding->device->bus == device->bus &&
            binding->device->device == device->device &&
            binding->device->function == device->function) {
            return NULL;
        }
    }

    if (g_binding_count >= PCI_MAX_DEVICES) {
        return NULL;
    }

    pci_core_binding_t* binding = &g_bindings[g_binding_count++];

    binding->device = device;
    binding->driver = NULL;

    return binding;
}

void pci_core_device_configured(PCI_Device* device) {
    if (!device) { return; }

    pci_core_binding_t* binding = pci_core_reserve_device(device);

    if (!binding) { return; }

    for (u16 i = 0; i < g_registered_driver_count; ++i) {
        pci_core_interface_driver_entry_t* reg = &g_registered_drivers[i];
        if (!match_interface(reg->match, device)) { continue; }

        IPCIDRIVER* drv = reg->create(device);
        if (!drv) { continue; }

        // TODO: not sure if this is the place to put this
        drv->pci_device = device;

        if (drv->init && !drv->init(drv)) {
            printf("[PCI CORE]: Failed to initialize driver '%s'\n", drv->name);
            kfree(drv);
            continue;
        }

        // TODO: may need probe here not sure
        task_t* task = task_create_suspended(NULL, class_driver_task_entry, (void*)drv);
        if (!task) {
            xassert(false, "");
            printf("[PCI CORE]: Failed to create task for driver '%s'\n", drv->name);
            kfree(drv);
            return;
        }

        // initialize things before allowing the task to execute
        task->name = drv->name;

        drv->task = task;
        binding->driver = drv;

        printf("[PCI CORE]: Bound %s to %02x:%02x.%x " "(vendor=%04x device=%04x class=%02x:%02x:%02x)\n", drv->name, device->bus, device->device, device->function, device->vendor_id, device->device_id, device->class_code, device->subclass, device->prog_if);

        // driver can now execute
        task_resume(task);
        
        break;
    }
}

void pci_core_register_driver(const char* name, pci_core_interface_match_t match, pci_core_interface_driver_factory_func factory) {
    pci_core_interface_driver_entry_t entry;
    entry.name = name;
    entry.create = factory;
    entry.match = match;

    if (g_registered_driver_count >= MAX_PCI_DRIVERS) {
        printf("Cannot insert PCI driver | g_registered_drivers hit max!\n");
        xassert(false, "");
        return;
    }

    g_registered_drivers[g_registered_driver_count] = entry;
    ++g_registered_driver_count;
}