#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

#define MAX_TASKS 16
#define TASK_STACK_SIZE (16 * 1024)

/*
 * These are software interrupts used only by the kernel scheduler.
 * irq_alloc_vector() starts at 0x40, so these don't collide with
 * dynamically allocated hardware IRQ vectors
 */
#define TASK_YIELD_VECTOR 0xF0
#define TASK_EXIT_VECTOR  0xF1

typedef enum {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_DEAD
} task_state_t;

typedef void (*task_entry_t)(void *arg);

typedef struct task {
    u64 id;
    task_state_t state;

    /*
     * Points at the saved interrupt frame:
     *   r15 ... rax
     *   int_no
     *   err_code
     *   rip
     *   cs
     *   rflags
     */
    u64 rsp;

    u8* stack;
    u64 stack_size;

    task_entry_t entry;
    void* arg;

    struct task *next;
} task_t;

typedef struct __packed__ task_initial_frame {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rbp;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;

    u64 int_no;
    u64 err_code;

    u64 rip;
    u64 cs;
    u64 rflags;

    u64 rsp;
    u64 ss;
} task_initial_frame_t;

void scheduler_init(u8 timer_vector);
void scheduler_start(void);
task_t* scheduler_current(void);

/**
 * @brief Called from the interrupt path. The current task's interrupt frame becomes its saved context. 
 * interrupted_rsp is the address of the full register frame created by isr_common_stub.
 * @returns then return whichever task's frame should be restored.
 */
u64 scheduler_handle_interrupt(u64 interrupted_rsp, u8 vector);

task_t* task_create(task_entry_t entry, void* arg);
void task_yield(); __attribute__((noreturn)) void task_exit();