#include <task.h>

#include <arch/x86_64/irq.h>
#include <xlibc/xassert.h>
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

#include <kernel.h>

extern ASMCALL void x86_64_restore_interrupt_context(u64 rsp);
extern ASMCALL void task_start_trampoline();

static task_t tasks[MAX_TASKS];
static u8 task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));

static u64 next_task_id;
static task_t* current_task;
static task_t* run_queue;

static u8 timer_vector;

// NOTE: entered through task_start_trampoline
static void task_bootstrap(task_t* task) {
    task->entry(task->arg);
    task_exit();
}

static void run_queue_add(task_t* task) {
    if (run_queue == NULL) {
        run_queue = task;
        task->next = task;
        return;
    }

    task->next = run_queue->next;
    run_queue->next = task;
}

#define KERNEL_CS 0x08
#define KERNEL_SS 0x10

static void task_build_initial_stack(task_t* task) {
    u64 stack_top = (u64)task->stack + task->stack_size;

    /*
     * This is the RSP the task will receive after iretq.
     * At task_start_trampoline entry we want:
     *     RSP % 16 == 8
     * so that a normal call instruction can align the stack correctly.
     */
    stack_top &= ~0xFULL;
    stack_top -= 8;

    /*
     * Build:
     *   registers
     *   int_no
     *   err_code
     *   RIP
     *   CS
     *   RFLAGS
     *   RSP
     *   SS
     */
    task_initial_frame_t* frame = (task_initial_frame_t* )(stack_top - sizeof(task_initial_frame_t));

    *frame = (task_initial_frame_t) {
        .r15 = 0,
        .r14 = 0,
        .r13 = 0,
        .r12 = (u64)task,

        .r11 = 0,
        .r10 = 0,
        .r9 = 0,
        .r8 = 0,

        .rbp = 0,
        .rdi = 0,
        .rsi = 0,
        .rdx = 0,
        .rcx = 0,
        .rbx = 0,
        .rax = 0,

        .int_no = 0,
        .err_code = 0,

        .rip = (u64)task_start_trampoline,
        .cs = KERNEL_CS,
        .rflags = 0x202,

        .rsp = stack_top,
        .ss = KERNEL_SS
    };

    task->rsp = (u64)frame;
}

void scheduler_init(u8 new_timer_vector) {
    timer_vector = new_timer_vector;

    current_task = NULL;
    run_queue = NULL;
    next_task_id = 1;

    for (u64 i = 0; i < MAX_TASKS; ++i) {
        tasks[i].id = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].rsp = 0;
        tasks[i].stack = NULL;
        tasks[i].stack_size = 0;
        tasks[i].entry = NULL;
        tasks[i].arg = NULL;
        tasks[i].next = NULL;
    }
}

task_t* scheduler_current() {
    return current_task;
}

task_t* task_create(task_entry_t entry, void* arg)
{
    if (entry == NULL)
        return NULL;

    task_t* task = NULL;

    for (u64 i = 0; i < MAX_TASKS; ++i) {
        if (tasks[i].state == TASK_UNUSED) {
            task = &tasks[i];
            break;
        }
    }

    if (task == NULL)
        return NULL;

    u64 index = (u64)(task - tasks);

    task->id = next_task_id++;
    task->state = TASK_RUNNABLE;

    task->stack = task_stacks[index];
    task->stack_size = TASK_STACK_SIZE;

    task->entry = entry;
    task->arg = arg;
    task->next = NULL;

    task_build_initial_stack(task);
    run_queue_add(task);

    return task;
}

/*
 * Find another runnable task after the current task.
 *
 * The current task is returned only when no other runnable task exists
 * and the current task is still running.
 *
 * This distinction matters for task_exit(): a dead task must never
 * be selected as the fallback task.
 */
static task_t* scheduler_next()
{
    if (current_task == NULL) {
        return run_queue;
    }

    task_t* candidate = current_task->next;
    task_t* start = candidate;

    do {
        if (candidate->state == TASK_RUNNABLE) {
            return candidate;
        }

        candidate = candidate->next;
    } while (candidate != start);

    if (current_task->state == TASK_RUNNING) {
        return current_task;
    }

    return NULL;
}

u64 scheduler_handle_interrupt(u64 interrupted_rsp, u8 vector)
{
    // before the scheduler starts there may still be interrupts, just resume whatever was interrupted
    if (current_task == NULL)
        return interrupted_rsp;

    // nothing scheduler-related happened
    if (vector != timer_vector &&
        vector != TASK_YIELD_VECTOR &&
        vector != TASK_EXIT_VECTOR) {
        return interrupted_rsp;
    }

    task_t* previous = current_task;

    // this frame is how the previous task is resumed later.
    previous->rsp = interrupted_rsp;

    // exit is special: don't consider the old task runnable.
    if (vector == TASK_EXIT_VECTOR) {
        previous->state = TASK_DEAD;

        task_t* next = scheduler_next();

        if (next == NULL) {
            xassert(false, "All tasks exited");

            for (;;)
                asm volatile("hlt");
        }

        next->state = TASK_RUNNING;
        current_task = next;

        return next->rsp;
    }

    /*
     * timer interrupt or voluntary yield, leave the current task TASK_RUNNING while selecting the next one
     * this allows scheduler_next() correctly fall back to it when it's the only runnable task.
     */
    task_t* next = scheduler_next();

    if (next == previous) {
        return previous->rsp;
    }

    previous->state = TASK_RUNNABLE;
    next->state = TASK_RUNNING;

    current_task = next;

    return next->rsp;
}

void scheduler_start()
{
    if (run_queue == NULL) {
        for (;;)
            asm volatile("hlt");
    }

    current_task = run_queue;

    // find first runnable task
    task_t* task = run_queue;

    for (u64 i = 0; i < MAX_TASKS; ++i) {
        if (task->state == TASK_RUNNABLE) {
            current_task = task;
            break;
        }

        task = task->next;
    }

    current_task->state = TASK_RUNNING;

    u64* frame = (u64*)current_task->rsp;

    // restore the manufactured interrupt frame, never returns
    x86_64_restore_interrupt_context(current_task->rsp);

    __builtin_unreachable();
}

void task_yield()
{
    asm volatile(
        "int $0xF0"
        :
        :
        : "memory", "cc"
    );
}

__attribute__((noreturn))
void task_exit()
{
    asm volatile(
        "int $0xF1"
        :
        :
        : "memory", "cc"
    );

    __builtin_unreachable();
}

// C half of the startup trampoline
void task_bootstrap_entry(task_t* task)
{
    task_bootstrap(task);
}