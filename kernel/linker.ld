ENTRY(kernel_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x00200000;

SECTIONS
{
    . = KERNEL_VMA;
    __kernel_start = .;

    .text : AT(KERNEL_LMA)
    {
        *(.text*)
    }

    .rodata : AT(KERNEL_LMA + (ADDR(.rodata) - KERNEL_VMA))
    {
        *(.rodata*)
    }

    .data : AT(KERNEL_LMA + (ADDR(.data) - KERNEL_VMA))
    {
        *(.data*)
    }

    . = ALIGN(4096);

    .bss.percpu : AT(KERNEL_LMA + (ADDR(.bss.percpu) - KERNEL_VMA))
    {
        __percpu_start = .;
        KEEP(*(.bss.percpu..base .bss.percpu..base.*))
        . = ALIGN(64);
        *(.bss.percpu..cacheline_aligned .bss.percpu..cacheline_aligned.*)
        *(.bss.percpu .bss.percpu.*)
        . = ALIGN(4096);
        __percpu_end = .;
    }

    ASSERT(__percpu_end - __percpu_start <= 0x2000, "Per-CPU section exceeds 8KB - increase CPU0_AREA_SIZE")

    .bss : AT(KERNEL_LMA + (ADDR(.bss) - KERNEL_VMA))
    {
        *(.bss*)
    }

    __kernel_end = .;
}