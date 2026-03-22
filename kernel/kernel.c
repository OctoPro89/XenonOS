#include <stdint.h>
#include "../shared/boot_info.h"
#include "arch/x86_64/hal.h"
#include "graphics/graphics.h"

#define ASMCALL __attribute__ ((__cdecl__))

#define COM1 0x3F8

void outb(unsigned short port, unsigned char val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void serial_write_char(char c) {
    outb(COM1, c);
}

void serial_write_str(const char* s) {
    while (*s) {
        serial_write_char(*s++);
    }
}

void serial_write_hex(uint64_t val) {
    const char* hex = "0123456789ABCDEF";

    serial_write_str("0x");

    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_write_char(hex[nibble]);
    }
}

void serial_write_dec(uint64_t val) {
    char buf[21]; // max for uint64_t = 20 digits + null
    int i = 0;

    // Special case: 0
    if (val == 0) {
        serial_write_char('0');
        return;
    }

    // Build digits in reverse
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    // Print in correct order
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}

void ASMCALL kernel_main_trampoline(BootInfo* bootInfo) {
    x86_64_HAL_init();
    serial_write_str("kernel_main_trampoline");

    uint32_t red   = convert_color(255,0,0, &bootInfo->fb);
    uint32_t green = convert_color(0,255,0, &bootInfo->fb);

    clear_screen(&bootInfo->fb, 0); // black

    // Square positions and velocities
    int x1 = 100, y1 = 50, vx1 = 2, vy1 = 1;
    int x2 = 500, y2 = 100, vx2 = -1, vy2 = 2;

    int w1 = 100, h1 = 100; // smaller squares for easier bouncing
    int w2 = 50, h2 = 50;

    while (1) {
        // Erase previous squares
        draw_rect(&bootInfo->fb, x1, y1, w1, h1, 0);
        draw_rect(&bootInfo->fb, x2, y2, w2, h2, 0);

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
        draw_rect(&bootInfo->fb, x1, y1, w1, h1, red);
        draw_rect(&bootInfo->fb, x2, y2, w2, h2, green);

        // Optional: draw text on top
        draw_string(&bootInfo->fb, "XenonOS v0.1", 100, 100, 0xFFFFFFFF);

        // Simple delay for visible animation
        for (volatile int i = 0; i < 1000000; i++);
    }
    // uint32_t *pixel = (uint32_t*)bootInfo->fb.BaseAddress;
    // for (uint32_t y = 0; y < bootInfo->fb.Height; y++) {
    //     for (uint32_t x = 0; x < bootInfo->fb.Width; x++) {
    //         pixel[y * bootInfo->fb.PixelsPerScanLine + x] = 0x0000FF00; // green
    //     }
    // }
    //outb(COM1, 0x49);
    //*(volatile int*)0xFFFFFFFFFFFFFFFF = 123;
    //outb(COM1, 0x50);
    while(1);
}