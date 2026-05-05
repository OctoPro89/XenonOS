#include "../shared/boot_info.h"
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <xlibc/stdio.h>
#include <arch/x86_64/hal.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/syscall.h>
#include <arch/x86_64/drivers/pci/pci.h>
#include <arch/x86_64/drivers/ahci/ahci.h>
#include <filesystem/block_device/block_device.h>
#include <filesystem/gpt/gpt.h>
#include <filesystem/fat32/fat32.h>
#include <filesystem/vfs/vfs.h>
#include <filesystem/vfs/vfs_fat32.h>
#include <graphics/graphics.h>
#include <memory/paging.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/heap.h>
#include "kernel.h"

#define KERNEL_VMA 0xFFFFFFFF80000000ULL
#define KERNEL_PMA 0x00200000ULL

#define KERNEL_STACK_SIZE 8192
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

uint8_t user_code[] = {
	0xb8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x3d, 
	0x1c, 0x00, 0x00, 0x00, 0xbe, 0x0c, 0x00, 0x00, 
	0x00, 0x0f, 0x05, 0xb8, 0x01, 0x00, 0x00, 0x00, 
	0x48, 0x8d, 0x3d, 0x15, 0x00, 0x00, 0x00, 0xbe, 
	0x03, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xeb, 0xfe, 
	0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 
	0x72, 0x6c, 0x64, 0x0a, 0x48, 0x69, 0x0a, 
};

#define USER_CODE_START  0x0000000000400000ULL
#define USER_STACK_TOP   0x0000000000800000ULL
#define USER_STACK_SIZE  (4 * PAGE_SIZE)

void setup_user_memory(vmm_space_t* space) {
    // Code
    PHYSICAL_ADDRESS code_phys = pmm_alloc_page();
    void* code_virt = (void*)phys_to_hhdm(code_phys);

    memcpy(code_virt, user_code, sizeof(user_code));

    u64 flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;
    flags &= ~(1ULL << 63);
    vmm_map(space, USER_CODE_START, code_phys, flags);

    // Stack
    for (int i = 0; i < 4; i++) {
        PHYSICAL_ADDRESS stack_phys = pmm_alloc_page();

        vmm_map(space,
            USER_STACK_TOP - (i + 1) * PAGE_SIZE,
            stack_phys,
            PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }
}

extern void ASMCALL enter_user_mode(u64 entry, u64 stack);

void run_user(vmm_space_t* space) {
    setup_user_memory(space);
    vmm_switch(space);
    enter_user_mode(USER_CODE_START, USER_STACK_TOP & ~0xF); // align stack

    while (1);
}

void draw_string(const char* str) {
    static u32 xoff = 200;
    static u32 yoff = 200;
    static u32 spacing = 10;

    graphics_draw_string(str, xoff, yoff, 0xFFFFFFFF);
    yoff += spacing;
    graphics_swap_buffers();
}

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    syscall_init();

    pmm_init(bootInfo);
    kernel_space.pml4 = (pte_t*)(bootInfo->PML4 + HHDM_OFFSET);

    graphics_init(&bootInfo->fb);
    graphics_clear_screen(0);

    draw_string("XenonOS v0.1");
    draw_string("Scanning for PCI devices...");

    pci_scan(); // find pci devices
    PCI_Device* ahci_dev = pci_find_ahci(); // find AHCI device
    if (ahci_dev == NULL) {
        serial_write_str("Failed to find AHCI device!\n");
        draw_string("Failed to find AHCI device!");
        while(1);
    }

    serial_write_str("Found AHCI device: ");
    draw_string("Found AHCI device");

    if (!ahci_dev) {
        serial_write_str("No AHCI\n");
        draw_string("No AHCI\n");
        while(1);
    }

    draw_string("Initializing AHCI driver");

    ahci_init(ahci_dev);
    ahci_probe_ports();

    // Pick first port
    HBA_PORT* port = ahci_get_port(0);

    draw_string("AHCI driver initialized successfully");

    block_device boot_disk = {
        .driver_data = (void*)port,
        .read = ahci_block_read,
    };

    draw_string("Finding FAT32 partition");

    u64 part_lba;

    if (!gpt_find_fat32(&boot_disk, &part_lba)) {
        serial_write_str("No FAT32 partition found\n");
        draw_string("No FAT32 partition found");
        while (1);
    }

    draw_string("Found FAT32 partition successfully");

    draw_string("Initializing FAT32 driver");

    FAT32_FS fs;
    fat32_init(&fs, &boot_disk, part_lba);
    fat32_list_root(&fs);

    draw_string("Initialized FAT32 driver successfully");

    draw_string("Setting up Virtual File System");

    vfs_mount_root(&fat32_ops, (void*)&fs);

    draw_string("Set up Virtual File System successfully");

    FILE* f = fopen("testlongfilename.txt", "r");
    if (!f) {
        serial_write_str("Failed to open file!\n");
        draw_string("Failed to open file!");
        while(1);
    }

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    serial_write_str("Filesize: ");
    serial_write_dec((u64)size);
    serial_write_str(" bytes\n");

    u8* buffer = kmalloc(26);
    fread(buffer, 10, 1, f);
    for (int i = 0; i < 10; ++i) { serial_write_char((char)buffer[i]); }

    serial_write_char('\n');

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { serial_write_char((char)buffer[i]); }

    fseek(f, -20, SEEK_CUR);

    fread(buffer, 16, 1, f);
    for (int i = 0; i < 16; ++i) { serial_write_char((char)buffer[i]); }

    free(buffer);
    fclose(f);

    serial_write_char('\n');
    serial_write_char('\n');

    vmm_space_t* space = vmm_create_space();
    space->user_mode = true;
    run_user(space);
    
    while(1);
}