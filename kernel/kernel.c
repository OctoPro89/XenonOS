#include "../shared/boot_info.h"
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <arch/x86_64/hal.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/drivers/pci/pci.h>
#include <arch/x86_64/drivers/ahci/ahci.h>
#include <filesystem/block_device/block_device.h>
#include <filesystem/gpt/gpt.h>
#include <filesystem/fat32/fat32.h>
#include <graphics/graphics.h>
#include "paging.h"
#include "heap.h"
#include "kernel.h"

#define KERNEL_VMA 0xFFFFFFFF80000000ULL
#define KERNEL_PMA 0x00200000ULL

uint64_t* current_pml4;

void kernel_setup_paging() {
    current_pml4 = alloc_page();
    uint64_t* old_pml4;
    asm volatile("mov %%cr3, %0" : "=r"(old_pml4));

    // Copy identity map (first 4 entries for safety)
    for (int i = 0; i < 4; i++) {
        current_pml4[i] = old_pml4[i];
    }

    uint64_t kernel_start = KERNEL_VMA;
    uint64_t phys_start   = KERNEL_PMA;

    // map a gb for now
    for (uint64_t off = 0; off < 0x40000000; off += 0x1000) {
        map_page(current_pml4,
            kernel_start + off,
            phys_start + off,
            PAGE_WRITABLE
        );
    }

    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));

    uint64_t stack_base = rsp & ~0xFFF;

    for (int i = 0; i < 16; i++) {
        map_page(current_pml4,
            stack_base - i * 0x1000,
            (stack_base - i * 0x1000) - KERNEL_VMA + KERNEL_PMA,
            PAGE_WRITABLE
        );
    }

    // Map test page
    asm volatile("mov %0, %%cr3" :: "r"(current_pml4));
}

void run_graphics_demo(BootInfo* bootInfo) {
    uint32_t red   = convert_color(255,0,0);
    uint32_t green = convert_color(0,255,0);

    graphics_init(&bootInfo->fb);
    clear_screen(0); // black

    // Square positions and velocities
    int x1 = 100, y1 = 50, vx1 = 2, vy1 = 1;
    int x2 = 500, y2 = 100, vx2 = -1, vy2 = 2;

    int w1 = 100, h1 = 100; // smaller squares for easier bouncing
    int w2 = 50, h2 = 50;

    while (1) {
        // Erase previous squares
        draw_rect(x1, y1, w1, h1, 0);
        draw_rect(x2, y2, w2, h2, 0);

        // Update positions
        x1 += vx1;
        y1 += vy1;
        x2 += vx2;
        y2 += vy2;

        // Bounce off edges
        if (x1 < 0) { x1 = 0; vx1 = -vx1; }
        if (y1 < 0) { y1 = 0; vy1 = -vy1; }
        if (x1 + w1 > bootInfo->fb.Width) { x1 = bootInfo->fb.Width - w1; vx1 = -vx1; }
        if (y1 + h1 > bootInfo->fb.Height) { y1 = bootInfo->fb.Height - h1; vy1 = -vy1; }

        if (x2 < 0) { x2 = 0; vx2 = -vx2; }
        if (y2 < 0) { y2 = 0; vy2 = -vy2; }
        if (x2 + w2 > bootInfo->fb.Width) { x2 = bootInfo->fb.Width - w2; vx2 = -vx2; }
        if (y2 + h2 > bootInfo->fb.Height) { y2 = bootInfo->fb.Height - h2; vy2 = -vy2; }

        // Draw squares at new positions
        draw_rect(x1, y1, w1, h1, red);
        draw_rect(x2, y2, w2, h2, green);

        draw_string("XenonOS v0.1", 100, 100, 0xFFFFFFFF);

        // Simple delay for visible animation
        //for (volatile int i = 0; i < 1000000; i++);
        graphics_swap_buffers(&bootInfo->fb);
    }
}

uint8_t user_code[] = {
	0xb8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x3d, 
	0x1c, 0x00, 0x00, 0x00, 0xbe, 0x0c, 0x00, 0x00, 
	0x00, 0x0f, 0x05, 0xb8, 0x01, 0x00, 0x00, 0x00, 
	0x48, 0x8d, 0x3d, 0x15, 0x00, 0x00, 0x00, 0xbe, 
	0x03, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xeb, 0xfe, 
	0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 
	0x72, 0x6c, 0x64, 0x0a, 0x48, 0x69, 0x0a, 
};

#define USER_CODE_ADDR 0x400000
#define USER_STACK_TOP 0x800000

void setup_user_memory(uint64_t* pml4) {
    void* code_page = alloc_page();

    // copy code
    for (int i = 0; i < sizeof(user_code); i++)
        ((uint8_t*)code_page)[i] = user_code[i];

    for (int i = 0; i < 4; i++) {
        void* stack_page = alloc_page();
        map_page(pml4, USER_STACK_TOP - (i+1)*0x1000, (uint64_t)stack_page,
                PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }

    for (uint64_t i = 0; i < 0x10000000; i += 0x1000) {
        map_page(pml4, i, i,
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }

    map_page(pml4, USER_CODE_ADDR, (uint64_t)code_page,
        PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
}

extern void ASMCALL enter_user_mode(u64 entry, u64 stack);

void run_user() {
    uint64_t* old_pml4;
    asm volatile("mov %%cr3, %0" : "=r"(old_pml4));

    uint64_t* user_pml4 = create_address_space(current_pml4);

    setup_user_memory(user_pml4);

    // Switch
    asm volatile("mov %0, %%cr3" :: "r"(user_pml4));

    enter_user_mode(USER_CODE_ADDR, USER_STACK_TOP - 8);

    while (1);
}

void fat32_list_root(FAT32_FS* fs) {
    uint32_t cluster = fs->root_cluster;

    uint32_t cluster_size = fs->sectors_per_cluster * 512;
    uint8_t* buf = malloc(cluster_size);

    while (cluster < 0x0FFFFFF8) {
        fat32_read_cluster(fs, cluster, buf);

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            FAT32_DIRECTORY_ENTRY* ent = (FAT32_DIRECTORY_ENTRY*)(buf + i);

            if (ent->name[0] == 0x00) return;
            if (ent->name[0] == 0xE5) continue;
            if (ent->attr == 0x0F) continue; // skip LFN

            char name[12];
            memcpy(name, ent->name, 11);
            name[11] = 0;

            serial_write_str("File: ");
            serial_write_str(name);
            serial_write_str(" Size: ");
            serial_write_dec(ent->size);
            serial_write_char('\n');
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }
}

void print_ahci_info(PCI_Device* ahci_dev) {
    for (int i = 0; i < 6; ++i) {
        serial_write_hex(ahci_dev->bar[i]);
        serial_write_char('\n');
    }
    serial_write_dec(ahci_dev->bus);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->class_code);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->device);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->device_id);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->device_id);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->function);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->prog_if);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->subclass);
    serial_write_char('\n');
    serial_write_dec(ahci_dev->vendor_id);
    serial_write_char('\n');
}

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    syscall_init();

    serial_write_char('\n');
    serial_write_char('\n');
    uint64_t rip;
    serial_write_str("Kernel RIP: ");
    asm volatile ("lea (%%rip), %0" : "=r"(rip));
    serial_write_hex(rip);
    serial_write_char('\n');
    serial_write_char('\n');

    kernel_setup_paging();

    pci_scan(); // find pci devices
    PCI_Device* ahci_dev = pci_find_ahci(); // find AHCI device
    if (ahci_dev == NULL) {
        serial_write_str("Failed to find AHCI device!\n");
        while(1);
    }

    serial_write_str("Found AHCI device: ");
    print_ahci_info(ahci_dev);

    if (!ahci_dev) {
        serial_write_str("No AHCI\n");
        while(1);
    }

    ahci_init(ahci_dev);
    ahci_probe_ports();

    // Pick first port
    HBA_PORT* port = ahci_get_port(0);
    ahci_port_init(port);

    block_device boot_disk = {
        .driver_data = (void*)port,
        .read = ahci_block_read,
    };

    uint64_t part_lba;

    if (!gpt_find_fat32(&boot_disk, &part_lba)) {
        serial_write_str("No FAT32 partition found\n");
        while (1);
    }

    FAT32_FS fs;
    fat32_init(&fs, &boot_disk, part_lba);
    fat32_list_root(&fs);

    FAT32_FILE* f = fat32_open(&fs, "TEST.TXT");
    if (!f) {
        serial_write_str("Failed to find TEST.TXT on disk!\n");
        while (1);
    }

    uint8_t buf[14];
    if (fat32_read(f, buf, 14) != 0) {
        for (int i = 0; i < 14; ++i) {
            serial_write_char((char)buf[i]);
        }
    }
        
    // run_user();
    // run_graphics_demo(bootInfo);
    while(1);
}