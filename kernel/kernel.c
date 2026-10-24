#include "../shared/boot_info.h"

#include <xlibc/string.h>
#include <memory/heap.h>
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
#include <drivers/usb/mass_storage/msc_driver.h>

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

#include <windowing/window/window.h>
#include <windowing/window/surface.h>
#include <windowing/window/events.h>
#include <windowing/window/internal.h>
#include <windowing/window/window_server.h>
#include <windowing/compositor/compositor.h>

#include <drivers/hwmon/amd_smn.h>
#include <drivers/hwmon/amd_zen_temp.h>

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

static void fs_test() {
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
    memset(buffer, 0, 26);
    fread(buffer, 10, 1, f);
    for (int i = 0; i < 10; ++i) { putc((char)buffer[i]); }

    printf("\n");

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { putc((char)buffer[i]); }

    fseek(f, -20, SEEK_CUR);

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { putc((char)buffer[i]); }

    kfree(buffer);
    fclose(f);

    printf("\n");
    printf("\n");
}

static void ahci_test() {
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

    printf("Initialized FAT32 driver successfully\n");
    printf("\n");

    printf("Setting up Virtual File System\n");

    vfs_mount_root(&fat32_ops, (void*)&fs);

    printf("Set up Virtual File System successfully\n");

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

    kfree(buffer);
    fclose(f);

    printf("\n");
    printf("\n");
}

void fat32_list_root(FAT32_FS* fs) {
    u32 cluster = fs->root_cluster;

    u32 cluster_size = fs->sectors_per_cluster * 512;
    u8* buf = kmalloc(cluster_size);
    memset(buf, 0, cluster_size);

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

        if (!fat32_read_fat_entry(fs, cluster, &cluster)) {
            kfree(buf);
            return;
        }
    }

    kfree(buf);
}

extern u64 hardware_frequency;

void timer_handler(struct regs* r, void* _) {
    ktimer_sched_irq_global_tick();
    scheduler_wake_sleepers(/* ktimer_get_system_time_in_nanoseconds() */ktimer_get_unscheduled_ns());
} 

static b8 boot_usb_ready = false;
static spinlock_t boot_usb_lock;
static wait_queue_t boot_usb_wq;

block_device boot_usb;
u64 part_lba;

void usb_drive_init(msc_driver_t* drv) {
    boot_usb = (block_device){
        .driver_data = (void*)drv,
        .read = msc_block_read,
    };
    
    printf("Finding FAT32 partition\n");

    if (!gpt_find_fat32(&boot_usb, &part_lba)) {
        printf("No FAT32 partition found\n");
        return;
    }

    printf("Found FAT32 partition successfully\n");

    u64 flags = 0;
    spin_lock_irqsave(&boot_usb_lock, &flags);
    boot_usb_ready = true;
    spin_unlock_irqrestore(&boot_usb_lock, flags);
    wait_queue_wake_all(&boot_usb_wq);
    msc_driver_set_initialize_notify_callback(NULL); // don't get another callback
}

void kernel_setup_usb_drivers() {
    usb_core_init();

    usb_core_register_driver("USB-HID DRIVER", USB_MAKE_MATCH(USB_CLASS_HID, USB_MATCH_ANY, USB_MATCH_ANY), hid_driver_factory);

#ifndef QEMU_DEBUG
    // registers USB Mass Storage interface using SCSI tranparent commands over Bulk-Only-Transport only
    usb_core_register_driver("USB-MSC DRIVER", USB_MAKE_MATCH(USB_CLASS_MASS_STORAGE, MSC_SUBCLASS_SCSI, MSC_PROTOCOL_BULK_ONLY), msc_driver_factory);

    // make sure to get one callback from boot USB
    msc_driver_set_initialize_notify_callback(usb_drive_init); // don't get another callback
#endif
}

u32 system_console_write_len = 0;
char system_console_buffer[1024];
spinlock_t system_console_lock;
b8 system_console_initialized = false;

void system_console_task_entry(void* arg) {
    (void)arg;

    window_config_t wconfig = {
        .title = "System Console",
        .rect = {
            .x = 780,
            .y = 120,
            .width = 400,
            .height = 300
        },
        .type = WINDOW_TYPE_NORMAL,
        .flags = 0
    };

    terminal_t system_console_term;
    window_t* system_console = window_server_create_window(&wconfig);

    if (!system_console) {
        return;
    }

    terminal_init(&system_console_term);
    terminal_attach_window(&system_console_term, system_console);

    u64 flags;

    spin_lock_irqsave(&system_console_lock, &flags);
    system_console_write_len = 0;
    system_console_initialized = true;
    spin_unlock_irqrestore(&system_console_lock, flags);

    char local_buffer[1024];

    /*
    for (;;) {
        size_t len;

        spin_lock_irqsave(&system_console_lock, &flags);

        len = system_console_write_len;

        if (len > 0) {
            memcpy(local_buffer, system_console_buffer, len);
            system_console_write_len = 0;
        }

        spin_unlock_irqrestore(&system_console_lock, flags);

        if (len > 0) {
            terminal_write(&system_console_term, local_buffer, len);
        } else {
            task_yield();
        }
    }
    */
}

void launch_shell_task_entry(void* arg) {
    (void)arg;

    process_t* process = process_create();
    if (!process) {
        printf("failed to create shell process\n");
        return;
    }

    int result = process_load_elf(process, "bin/sh");
    if (result < 0) {
        printf("failed to load /bin/sh: %d\n", result);
        process_destroy(process);
        return;
    }

    // set working directory, defaults to root
    strncpy(process->cwd, "/", PROCESS_CWD_MAX);

    result = process_start(process);
    if (result < 0) {
        printf("failed to start shell: %d\n", result);
        process_destroy(process);
        return;
    }

    int exit_code = process_wait(process);
    printf("shell exited with code %d\n", exit_code);
    process_reap(process);
    task_exit();
}

spinlock_t launch_program_path_lock;
char launch_program_path[256];

void launch_program_entry(void* arg) {
    char path[256]; // don't hold the lock whilst doing all of this
    u64 flags = 0;
    spin_lock_irqsave(&launch_program_path_lock, &flags);
    strncpy(path, launch_program_path, 255);
    spin_unlock_irqrestore(&launch_program_path_lock, flags);
    
    process_t* process = process_create();
    if (!process) {
        return;
    }

    int result = process_load_elf(process, path);
    if (result < 0) {
        process_destroy(process);
        return;
    }

    // set working directory, defaults to root
    strncpy(process->cwd, "/", PROCESS_CWD_MAX);

    result = process_start(process);
    if (result < 0) {
        process_destroy(process);
        return;
    }

    (void)process_wait(process);
    process_reap(process);
    task_exit();
}

// TODO: resource cleanup
static b8 xenon_spawn_program(const char* absolute_path) {
    if (absolute_path) {
        u64 flags = 0;
        spin_lock_irqsave(&launch_program_path_lock, &flags);
        strncpy(launch_program_path, absolute_path, 255);
        spin_unlock_irqrestore(&launch_program_path_lock, flags);

        task_t* t = task_create(NULL, launch_program_entry, (void*)NULL);
        return t != NULL;
    }
    
    return false;
}

static void kernel_main(void* arg) {
    if (!input_init()) {
        xassert(false, "");
    }
    kernel_setup_usb_drivers();

    printf("XenonOS v0.1\n");

    printf("Scanning for PCI devices...\n");

    pci_scan(); // find pci devices

    if (amd_smn_init()) {
        i32 temp_mc;

        if (amd_zen_read_temperature(&temp_mc)) {
            i32 whole = temp_mc / 1000;
            i32 fraction = temp_mc % 1000;

            // keep the fractional part positive for negative temperatures
            if (fraction < 0) {
                fraction = -fraction;
            }

            printf("CPU temperature raw millidegrees: %d\n", temp_mc);
            printf("CPU temperature: %d.%d C\n", whole, fraction);
        } else {
            printf("CPU temperature unavailable\n");
        }
    } else {
        printf("AMD SMN unavailable\n");
    }

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

    printf("Creating XHCI driver task!\n");
    task_t* xhci_driver_task = task_create(NULL, xhci_driver_task_entry, (void*)&xhci_driver);

#ifndef QEMU_DEBUG
    // wait until boot USB drive is set up before launching shit
    u64 flags = 0;
    spin_lock_irqsave(&boot_usb_lock, &flags);
    while (!boot_usb_ready) {
        flags = task_wait(&boot_usb_wq, &boot_usb_lock, flags);
    }
    spin_unlock_irqrestore(&boot_usb_lock, flags);

    printf("Initializing FAT32 driver\n");

    FAT32_FS fs;
    fat32_init(&fs, &boot_usb, part_lba);
    fat32_list_root(&fs);

    printf("Initialized FAT32 driver successfully\n");
    printf("\n");

    printf("Setting up Virtual File System\n");

    vfs_mount_root(&fat32_ops, (void*)&fs);

    printf("Set up Virtual File System successfully\n");

    VFS_DIR* dir = vfs_opendir("/");

    if (!dir) {
        serial_write_str("opendir failed\n");
        return;
    }

    VFS_DIRENT entry;

    for (;;) {
        int result = vfs_readdir(dir, &entry);

        if (result == 0) {
            break; // End of directory
        }

        if (result < 0) {
            serial_write_str("readdir failed\n");
            break;
        }

        serial_write_str(entry.name);

        if (entry.type == VFS_TYPE_DIRECTORY) {
            serial_write_str("/");
        }

        serial_write_str("\n");
    }

    vfs_closedir(dir);
#endif

    // boot USB plugged in and set up
    window_server_init();
    xenon_surface_t framebuffer_surface;
    framebuffer_surface.height = graphics_get_framebuffer_height();
    framebuffer_surface.width = graphics_get_framebuffer_width();
    framebuffer_surface.stride = graphics_get_framebuffer_stride();
    if (!compositor_init(&framebuffer_surface)) {
        printf("Failed to init compositor!\n");
        for (;;);
    }

#ifndef QEMU_DEBUG
    if (!compositor_set_wallpaper("haimiya.png")) {
        // TODO:
        printf("Failed to load wallpaper image!\n");
    }
#endif

    compositor_set_program_spawn_fn(xenon_spawn_program);

    // task_t* system_console_task = task_create(NULL, system_console_task_entry, NULL);

#ifndef QEMU_DEBUG
    printf("Launching usermode shell\n");
    task_t* launch_shell_task = task_create(NULL, launch_shell_task_entry, NULL);
#endif

    for (;;) {
        compositor_process_events();
        compositor_render();

        task_yield();
    }
}

#include <arch/x86_64/msr.h>

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

    // needs to run before any printing
    spin_lock_init(&system_console_lock);
    spin_lock_init(&launch_program_path_lock);

    spin_lock_init(&boot_usb_lock);
    wait_queue_init(&boot_usb_wq);

    printf("Starting kernel main task!\n");

    ktimer_calibrate_cpu_timer(1);

    scheduler_init(timer_irq_vector);
    
    task_t* main_thread = task_create(NULL, kernel_main, NULL);

    ktimer_start_cpu_periodic_timer(timer_irq_vector); // get timer interrupts
    lapic_unmask_timer_irq(); // unmask from calibration
    scheduler_start();
    
    while(1);
}