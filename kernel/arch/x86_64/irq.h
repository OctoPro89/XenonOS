#pragma once
#include <xlibc/xstdint.h>

// generalized IRQs
#define IRQ0   32
#define IRQ1   33
#define IRQ2   34
#define IRQ3   35
#define IRQ4   36
#define IRQ5   37
#define IRQ6   38
#define IRQ7   39
#define IRQ8   40
#define IRQ9   41
#define IRQ10  42
#define IRQ11  43
#define IRQ12  44
#define IRQ13  45
#define IRQ14  46
#define IRQ15  47
#define IRQ16  48
#define IRQ17  49
#define IRQ18  50
#define IRQ19  51
#define IRQ20  52
#define IRQ21  53
#define IRQ22  54
#define IRQ23  55
#define IRQ24  56
#define IRQ25  57
#define IRQ26  58
#define IRQ27  59
#define IRQ28  60
#define IRQ29  61
#define IRQ30  62
#define IRQ31  63
#define IRQ32  64
#define IRQ33  65
#define IRQ34  66
#define IRQ35  67
#define IRQ36  68
#define IRQ37  69
#define IRQ38  70
#define IRQ39  71
#define IRQ40  72
#define IRQ41  73
#define IRQ42  74
#define IRQ43  75
#define IRQ44  76
#define IRQ45  77
#define IRQ46  78
#define IRQ47  79
#define IRQ48  80
#define IRQ49  81
#define IRQ50  82
#define IRQ51  83
#define IRQ52  84
#define IRQ53  85
#define IRQ54  86
#define IRQ55  87
#define IRQ56  88
#define IRQ57  89
#define IRQ58  90
#define IRQ59  91
#define IRQ60  92
#define IRQ61  93
#define IRQ62  94
#define IRQ63  95
#define IRQ64  96

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    uint64_t int_no, err_code;

    // NOTE: user_rsp and ss may not be push from ring0 -> ring0 ints
    uint64_t rip, cs, rflags, user_rsp, ss; 
};

typedef void (*irq_handler_t)(struct regs* r, void* user_data);

void irq_register_handler(uint8_t vec, irq_handler_t fn, void* user_data);
void irq_dispatch(struct regs* r);
u8 irq_alloc_vector();