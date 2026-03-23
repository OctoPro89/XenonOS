ENTRY(kernel_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x00200000;

SECTIONS
{
    . = KERNEL_VMA;

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

    .bss : AT(KERNEL_LMA + (ADDR(.bss) - KERNEL_VMA))
    {
        *(.bss*)
    }
}