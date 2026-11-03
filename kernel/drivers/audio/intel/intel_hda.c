#include <drivers/audio/intel/intel_hda.h>
#include <memory/allocators/dma_allocator.h>
#include <memory/heap.h>
#include <memory/paging.h>
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <time/time.h>
#include <task.h>

#define HDA_MAX_CODECS 30
#define HDA_GCTL_CRST (1 << 0)
#define STATESTS_INTEL_HDA_ALL_CODECS 0x7FFF

b8 intel_hda_driver_init_driver(void* self) {
    intel_hda_driver_t* driver = (intel_hda_driver_t*)self;
    if (!driver || !driver->pci_device) { return false; }
    
    vaddr_t hda_base = pci_map_bar(driver->pci_device, 0);
    if (hda_base == 0) { return false; }

    driver->regs = (volatile intel_hda_regs_t*)hda_base;

    // driver->codecs = kmalloc(sizeof(intel_hda_codec_t) * HDA_MAX_CODECS);
    driver->num_codecs = 0;

    u8 num_iss = (driver->regs->gcap >> 0) & 0x0F;
    u8 num_oss = (driver->regs->gcap >> 4) & 0x0F;
    printf("[INTEL HDA]: Major %d, Minor %d | ISS: %d, OSS: %d\n", driver->regs->vmaj, driver->regs->vmin, num_iss, num_oss);    

    return true;
}

// reference: linux
b8 intel_hda_driver_reset_controller(intel_hda_driver_t* driver) {
    if (!driver || !driver->regs) { return false; }

    // clear state change / wake statuses
    driver->regs->statests = STATESTS_INTEL_HDA_ALL_CODECS;

    // force CRST bit low to enter reset state
    driver->regs->gctl &= ~HDA_GCTL_CRST;

    // poll until hardware confirms it has entered the reset state
    const u32 timeout = 100;
    u32 ms_waited = 0;
    while ((driver->regs->gctl & HDA_GCTL_CRST) != 0) {
        msleep(1);
        if (++ms_waited > timeout) {
            printf("[INTEL HDA]: Error - Controller timed out entering link reset.\n");
            return false;
        }
    }

    // TODO: not sure if this is required but probably not a bad idea
    nsleep(100000); // 100 microseconds hold time

    // assert CRST back to 1.
    driver->regs->gctl |= HDA_GCTL_CRST;

    while ((driver->regs->gctl & HDA_GCTL_CRST) == 0) {
        msleep(1);
        if (++ms_waited > timeout) {
            printf("[INTEL HDA]: Error - Controller timed out exiting link reset.\n");
            return false;
        }
    }

    // linux does this
    msleep(2);

    return true;
}

void intel_hda_driver_run(void* _) {
    while (1) {
        task_yield();
    }
}

IPCIDRIVER* intel_hda_driver_factory(PCI_Device* dev) {
    intel_hda_driver_t* drv = kmalloc(sizeof(intel_hda_driver_t));
    xassert(drv, "Failed to allocate driver!\n");
    if (!drv) { return NULL; }
    memset(drv, 0, sizeof(intel_hda_driver_t));
    drv->name = "Intel High Definition Audio Driver";
    // INTERFACE ASSIGN
    drv->init = intel_hda_driver_init_driver;
    drv->run = intel_hda_driver_run;
    return (IPCIDRIVER*)drv;
}