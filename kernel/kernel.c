#include "../shared/boot_info.h"
#include <arch/x86_64/hal.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/syscall.h>
#include <graphics/graphics.h>
#include "paging.h"
#include "kernel_memory.h"
#include "kernel.h"

#define KERNEL_VMA 0xFFFFFFFF80000000ULL
#define KERNEL_PMA 0x00200000ULL

uint64_t* current_pml4;

void kernel_setup_paging_and_heap() {
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

    kheap_init((void*)0x300000, 0x5000000); // 80 MB heap
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
    0x0F, 0x05,   // syscall
    0xEB, 0xFE    // infinite loop
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

extern void enter_user_mode(uint64_t entry, uint64_t stack);

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

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    syscall_init();

    uint64_t rip;
    asm volatile ("lea (%%rip), %0" : "=r"(rip));
    serial_write_hex(rip);

    kernel_setup_paging_and_heap();
    run_user();
    // run_graphics_demo(bootInfo);
    while(1);
}