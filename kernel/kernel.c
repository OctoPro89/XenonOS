#include "../shared/boot_info.h"
#include "arch/x86_64/hal.h"
#include "arch/x86_64/io.h"
#include "graphics/graphics.h"
#include "kernel_memory.h"
#include "kernel.h"

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    uint64_t rip;
    asm volatile ("lea (%%rip), %0" : "=r"(rip));
    serial_write_hex(rip);

    kheap_init((void*)0x300000, 0x5000000); // 80 MB heap

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
    while(1);
}