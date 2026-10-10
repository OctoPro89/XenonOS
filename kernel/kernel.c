#include "../shared/boot_info.h"

#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <xlibc/stdio.h>

#include <acpi/acpi.h>

#include <arch/x86_64/hal.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/apic/lapic.h>
#include <arch/x86_64/apic/apic_timer.h>
#include <arch/x86_64/drivers/pci/pci.h>
#include <arch/x86_64/drivers/ahci/ahci.h>

#include <drivers/usb/xhci.h>
#include <drivers/usb/xhci_mem.h>

#include <drivers/usb/core/usb_core.h>
#include <drivers/usb/hid/hid_driver.h>

#include <drivers/input/input.h>

#include <filesystem/block_device/block_device.h>
#include <filesystem/gpt/gpt.h>
#include <filesystem/fat32/fat32.h>
#include <filesystem/vfs/vfs.h>
#include <filesystem/vfs/vfs_fat32.h>

#include <task.h>
#include <process.h>

#include <tty/terminal.h>

#include <elf/elf.h>

#include <graphics/graphics.h>

#include <de/de.h>

#include <memory/paging.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/heap.h>

#include <time/time.h>

#include "kernel.h"

#define KERNEL_VMA 0xFFFFFFFF80000000ULL
#define KERNEL_PMA 0x00200000ULL

#define KERNEL_STACK_SIZE (8192 * 2)
static u8 kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
uint64_t kernel_stack_top = (u64)(((u8*)kernel_stack) + KERNEL_STACK_SIZE);

void fat32_list_root(FAT32_FS* fs) {
    u32 cluster = fs->root_cluster;

    u32 cluster_size = fs->sectors_per_cluster * 512;
    u8* buf = malloc(cluster_size);

    while (cluster < 0x0FFFFFF8) {
        fat32_read_cluster(fs, cluster, buf);

        for (u32 i = 0; i < cluster_size; i += 32) {
            FAT32_DIRECTORY_ENTRY* ent = (FAT32_DIRECTORY_ENTRY*)(buf + i);

            if (ent->name[0] == 0x00) return;
            if (ent->name[0] == 0xE5) continue;
            if (ent->attr == 0x0F) continue; // skip LFN5

            char name[12];
            memcpy(name, ent->name, 11);
            name[11] = 0;

            printf("\tFile: %s Size: %u\n", name, ent->size);
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }
}

void timer_handler(struct regs* r, void* _) {
    ktimer_sched_irq_global_tick();
    scheduler_wake_sleepers(ktimer_get_system_time_in_nanoseconds());
} 

void kernel_assign_usb_drivers() {
    usb_core_register_driver("USB-HID DRIVER", USB_MAKE_MATCH(USB_CLASS_HID, USB_MATCH_ANY, USB_MATCH_ANY), hid_driver_factory);
}

#include <tty/terminal.h>

#include <process.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>

static void launch_shell() {
    process_t* process = process_create();

    if (!process) {
        printf("failed to create shell process\n");
        return;
    }

    int result = process_load_elf(process,"bin/sh");

    if (result < 0) {
        printf("failed to load /bin/sh: %d\n", result);

        process_destroy(process);
        return;
    }

    result = process_start(process);

    if (result < 0) {
        printf("failed to start /bin/sh: %d\n", result);

        process_destroy(process);
        return;
    }

    // TODO:
    int exit_code = process_wait(process);
    printf("shell exited with code %d\n", exit_code);
    process_destroy(process);
}

static void kernel_main(void* arg) {
    if (!input_init()) {
        xassert(false, "");
    }
    kernel_assign_usb_drivers();

    printf("XenonOS v0.1\n");

    terminal_init(console_terminal());
    terminal_start_input_task(console_terminal());

    printf("Scanning for PCI devices...\n");

    pci_scan(); // find pci devices

    PCI_Device* xhci_dev = pci_find_xhci(); // find XHCI device
    if (xhci_dev == NULL) {
        printf("Failed to find XHCI device!\n");
        while(1);
    }

    printf("\n");
    printf("Found XHCI device\n");

    printf("Initializing XHCI driver\n");
    xhci_driver_t xhci_driver;
    xhci_driver.name = "Default XHCI Controller Driver";
    xhci_driver.pci_device = xhci_dev;
    if (!xhci_driver_init_driver(&xhci_driver) /* || !xhci_driver_start_device(&xhci_driver) */) {
        printf("Failed to initialize XHCI driver!\n");
        while(1);
    }

    printf("XHCI Controller Mapping Info:\n");

    printf("    Virtual Base: ");
    printf("%p\n", xhci_driver.xhc_base);
    printf("    BAR Address: ");
    printf("%p\n", xhci_driver.pci_device->bar[0].base);
    printf("\n");

    // xhci_driver_log_capability_registers(&xhci_driver);

    printf("XHCI Driver initialized successfully\n");
    printf("\n");

    /*
    PCI_Device* ahci_dev = pci_find_ahci(); // find AHCI device
    if (ahci_dev == NULL) {
        printf("Failed to find AHCI device!\n");
        while(1);
    }

    printf("Found AHCI device\n");

    printf("Initializing AHCI driver\n");
    printf("AHCI Info:\n");

    ahci_init(ahci_dev);
    ahci_probe_ports();

    // Pick first port
    HBA_PORT* port = ahci_get_port(0);

    printf("AHCI driver initialized successfully\n");

    printf("\n");

    block_device boot_disk = {
        .driver_data = (void*)port,
        .read = ahci_block_read,
    };

    printf("Finding FAT32 partition\n");

    u64 part_lba;

    if (!gpt_find_fat32(&boot_disk, &part_lba)) {
        printf("No FAT32 partition found\n");
        while (1);
    }

    printf("Found FAT32 partition successfully\n");

    printf("Initializing FAT32 driver\n");

    FAT32_FS fs;
    fat32_init(&fs, &boot_disk, part_lba);
    fat32_list_root(&fs);

    printf("Initialized FAT32 driver successfully\n");
    printf("\n");

    printf("Setting up Virtual File System\n");

    vfs_mount_root(&fat32_ops, (void*)&fs);

    printf("Set up Virtual File System successfully\n");
    */

    /*
    FILE* f = fopen("testlongfilename.txt", "r");
    if (!f) {
        printf("Failed to open file!\n");
        while(1);
    }

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("Filesize: %u bytes\n", size);

    u8* buffer = kmalloc(26);
    fread(buffer, 10, 1, f);
    for (int i = 0; i < 10; ++i) { putc((char)buffer[i]); }

    printf("\n");

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { putc((char)buffer[i]); }

    fseek(f, -20, SEEK_CUR);

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { putc((char)buffer[i]); }

    free(buffer);
    fclose(f);

    printf("\n");
    printf("\n");
    */

    printf("Creating XHCI driver task!\n");
    task_t* xhci_driver_task = task_create(NULL, xhci_driver_task_entry, (void*)&xhci_driver);

    printf("Launching usermode shell\n");
    // launch_shell();

    task_t* de_task = task_create(NULL, desktop_env_task_entry, NULL);
    for (;;) {
        task_yield();
    }

    u64 last_second = 0;

    for (;;) {
        u64 now = ktimer_get_system_time_in_seconds();

        if (now != last_second) {
            char buf[100];

            snprintf(buf, sizeof(buf), "System Uptime (seconds): %llu", now);

            graphics_draw_rect(500, 50, 220, 50, 0x0);
            graphics_draw_string(buf, 500, 50, 0xFFFF);

            last_second = now;
        }

        task_sleep_ms(10);
    }
}

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    syscall_init();

    pmm_init(bootInfo);
    kernel_space.pml4 = (pte_t*)(bootInfo->PML4 + HHDM_OFFSET);

    acpi_enumerate_acpi_tables((void*)bootInfo->AcpiRsdp);

    u8 timer_irq_vector = irq_alloc_vector();
    irq_register_handler(timer_irq_vector, &timer_handler, NULL);

    lapic_init();

    graphics_init(&bootInfo->fb);
    graphics_clear_screen(0);

    printf("Starting kernel main task!\n");

    ktimer_calibrate_cpu_timer(1);

    scheduler_init(timer_irq_vector);
    
    task_t* main_thread = task_create(NULL, kernel_main, NULL);

    ktimer_start_cpu_periodic_timer(timer_irq_vector); // get timer interrupts
    scheduler_start();
    
    while(1);
}