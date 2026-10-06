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

uint8_t user_code[] = {
	0xb8, 0x01, 0x00, 0x00, 0x00, 0xbf, 0x01, 0x00, 
	0x00, 0x00, 0x48, 0x8d, 0x35, 0x21, 0x00, 0x00, 
	0x00, 0xba, 0x0e, 0x00, 0x00, 0x00, 0x0f, 0x05, 
	0xb8, 0x00, 0x00, 0x00, 0x00, 0xbf, 0x00, 0x00, 
	0x00, 0x00, 0x48, 0x8d, 0x35, 0x17, 0x00, 0x00, 
	0x00, 0xba, 0x80, 0x00, 0x00, 0x00, 0x0f, 0x05, 
	0xeb, 0xce, 0x78, 0x65, 0x6e, 0x6f, 0x6e, 0x6f, 
	0x73, 0x40, 0x78, 0x65, 0x3a, 0x2f, 0x24, 0x20, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void setup_user_memory(vmm_space_t* space) {
    // code
    paddr_t code_phys = pmm_alloc_page();
    void* code_virt = (void*)phys_to_hhdm(code_phys);

    memcpy(code_virt, user_code, sizeof(user_code));

    u64 flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;
    flags &= ~(1ULL << 63);
    vmm_map(space, USER_CODE_START, code_phys, flags);

    // Stack
    for (int i = 0; i < 4; i++) {
        paddr_t stack_phys = pmm_alloc_page();

        vmm_map(space, USER_STACK_TOP - (i + 1) * PAGE_SIZE, stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }
}

extern void ASMCALL enter_user_mode(u64 entry, u64 stack) __attribute__((noreturn));

void run_user(vmm_space_t* space) {
    setup_user_memory(space);
    vmm_switch(space);
    enter_user_mode(USER_CODE_START, USER_STACK_TOP & ~0xF); // align stack

    while (1);
}

void timer_handler(struct regs* r, void* _) {
    ktimer_sched_irq_global_tick();
    scheduler_wake_sleepers(ktimer_get_system_time_in_nanoseconds());
} 

void kernel_assign_usb_drivers() {
    usb_core_register_driver("USB-HID DRIVER", USB_MAKE_MATCH(USB_CLASS_HID, USB_MATCH_ANY, USB_MATCH_ANY), hid_driver_factory);
}

static void user_task_entry(void* arg) {
    process_t* process = arg;

    setup_user_memory(process->space);
    enter_user_mode(USER_CODE_START, USER_STACK_TOP & ~0xFULL);

    __builtin_unreachable();
}

void launch_test_process(void) {
    process_t* process;
    task_t* task;

    process = process_create();

    if (!process) {
        xassert(false, "failed to create test process");
    }

    task = task_create(
        process,
        user_task_entry,
        process
    );

    if (!task) {
        xassert(false, "failed to create user task");
    }
}

#include <tty/terminal.h>

#include <process.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>

#define SHELL_LINE_SIZE 256

static const char shell_prompt[] = "xenonos@xe:/$ ";

static void shell_print( terminal_t* terminal, const char* str) {
    terminal_write(terminal, str, strlen(str));
}

static void shell_run_program(const char* command) {
    char path[256];

    if (command[0] == '/') {
        strncpy(path, command, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
    } else {
        snprintf(path, sizeof(path), "bin/%s", command);
    }

    printf("[SHELL] launching %s\n",path);

    process_t* process = process_create();

    if (!process) {
        printf("[SHELL] failed to create process\n");
        return;
    }

    int result = process_load_elf(process, path);

    if (result < 0) {
        printf("[SHELL] failed to load %s: %d\n", path, result);
        process_destroy(process);
        return;
    }

    printf("[SHELL] before launch\n");
    debug_current_address_space();

    result = process_start(process);

    if (result < 0) {
        printf("[SHELL] failed to start %s: %d\n", path, result);
        process_destroy(process);
        return;
    }

    printf("[SHELL] waiting\n");
    debug_current_address_space();

    int exit_code = process_wait(process);

    printf("\n[SHELL] process exited with code %d\n", exit_code);

    printf("[SHELL] after wait\n");
    debug_current_address_space();

    process_destroy(process);
}

static void shell_task(void* arg) {
    terminal_t* terminal = arg;

    char line[SHELL_LINE_SIZE];

    for (;;) {
        shell_print(terminal, shell_prompt);

        ssize_t size = terminal_read(terminal, line, sizeof(line) - 1);

        if (size <= 0) {
            continue;
        }

        line[size] = 0;

        if (size > 0 && line[size - 1] == '\n') {
            line[size - 1] = 0;
        }

        if (line[0] == 0) {
            continue;
        }

        if (strcmp(line, "exit") == 0) {
            shell_print(terminal, "\n");
            continue;
        }

        shell_run_program(line);
    }
}

void shell_start() {
    task_create(NULL, shell_task, console_terminal());
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

    // printf("Launching usermode process\n");
    // launch_test_process();
    shell_start();

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

    ktimer_calibrate_cpu_timer(4);

    scheduler_init(timer_irq_vector);
    
    task_t* main_thread = task_create(NULL, kernel_main, NULL);

    ktimer_start_cpu_periodic_timer(timer_irq_vector); // get timer interrupts
    scheduler_start();
    
    while(1);
}