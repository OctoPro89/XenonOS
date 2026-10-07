#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

#define MAX_TASKS 32
#define TASK_STACK_SIZE (16 * 1024)

/*
 * These are software interrupts used only by the kernel scheduler.
 * irq_alloc_vector() starts at 0x40, so these don't collide with
 * dynamically allocated hardware IRQ vectors
 */
#define TASK_YIELD_VECTOR 0xF0
#define TASK_EXIT_VECTOR  0xF1
#define TASK_BLOCK_VECTOR 0xF2
#define TASK_SLEEP_VECTOR 0xF3

typedef enum {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_DEAD
} task_state_t;

typedef void (*task_entry_t)(void* arg);

typedef struct task task_t;
typedef struct wait_queue wait_queue_t;
typedef struct spinlock spinlock_t;
typedef struct process process_t;

struct wait_queue {
    task_t* head;
    task_t* tail;
};

struct task {
    // SYSCALL state, accessed by syscall.asm
    // NOTE: they need to be here exactly and in this order
    u64 syscall_user_rsp;
    u64 syscall_user_rip;
    u64 syscall_user_rflags;
    u64 kernel_stack_top;

    u64 id;
    const char* name;
    task_state_t state;

    process_t* process; // resource container shared by this task's threads

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

    // scheduler run queue
    task_t* next;

    // wait queue linkage
    task_t* wait_next;
    wait_queue_t *waiting_on;

    // scheduler sleep deadline
    u64 wake_deadline_ns;

    // idle task never competes with normal runnable tasks unless no normal task can run
    b8 is_idle;
};

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

void scheduler_wake_sleepers(u64 now_ns);
void scheduler_request_reschedule();

/**
 * @brief Creates a new process
 * 
 * @param process NULL for a kernel task, the parent userspace process otherwise
 * @param entry Entry function for the task to start executing
 * @param arg Pointer to an argument to call `entry` with
 * 
 * @returns A pointer to the created task
 */
task_t* task_create(process_t* process, task_entry_t entry, void* arg);
void task_yield();
__attribute__((noreturn)) void task_exit();

void task_sleep_ns(uint64_t ns);
void task_sleep_ms(uint64_t ms);

void task_block_on(wait_queue_t* queue);

/**
 * @note Caller must have interrupts disabled.
 * Does not return until this task is awakened.
 */
void task_block_current_locked(wait_queue_t* queue);

/**
 * @brief Blocks the current task on `queue` while atomically releasing `lock`,
 * 
 * @note
 * PRECONDITIONS:
 *  - `lock` is held by the current execution context
 *  - interrupts are disabled
 *  - `flags` is the value returned by spin_lock_irqsave()
 * 
 * POSTCONDITIONS:
 *  - `lock` is held
 *  - interrupts are disabled
 *  - returned value is the IRQ state saved when reacquiring
 */
u64 task_wait(wait_queue_t* queue, spinlock_t* lock, u64 flags);

/**
 * @brief Removes a task from the scheduler's run queue and resets it 
 */
void task_reap(task_t* task);

void wait_queue_init(wait_queue_t* queue);

void wait_queue_wake_one(wait_queue_t* queue);
void wait_queue_wake_all(wait_queue_t* queue);

/*
 * @note Caller must have interrupts disabled.
 */
void wait_queue_wake_one_locked(wait_queue_t* queue);
void wait_queue_wake_all_locked(wait_queue_t* queue);