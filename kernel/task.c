#include <task.h>
#include <arch/x86_64/context_switch.h>
#include <xlibc/xassert.h>
#include <xlibc/xstddef.h>

static task_t tasks[MAX_TASKS];
static u8 task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));
static u64 next_task_id = 1;
static task_t* current_task = NULL;
static task_t* run_queue = NULL;

/**
 * Entered by the manufactured initial task context.
 * 
 * Use R12 to carry the task pointer into this trampoline,
 * context_switch restores it from the task's initial stack the ret jumps here
 */
extern ASMCALL void task_start_trampoline();

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

// construct the initial stack frame expected by x86_64_context_switch()
static void task_build_initial_stack(task_t* task) {
    u64 stack_top = (u64)task->stack + task->stack_size;
    stack_top &= ~0xFULL;

    /*
     * x86_64_context_switch() expects this exact layout:
     *
     *   rsp -> r15
     *          r14
     *          r13
     *          r12
     *          rbp
     *          rbx
     *          return RIP
     */
    u64* sp = (u64*)stack_top;
    sp -= 7;

    sp[0] = 0;                              // r15
    sp[1] = 0;                              // r14
    sp[2] = 0;                              // r13
    sp[3] = (u64)task;                      // r12
    sp[4] = 0;                              // rbp
    sp[5] = 0;                              // rbx
    sp[6] = (u64)task_start_trampoline;     // return RIP

    task->rsp = (u64)sp;

    serial_write_str("task rsp = ");
    serial_write_hex(task->rsp);
    serial_write_str("\r\n");

    serial_write_str("initial RIP = ");
    serial_write_hex(sp[6]);
    serial_write_str("\r\n");
}

void scheduler_init() {
    current_task = NULL;
    run_queue = NULL;
    next_task_id = 1;

    for (uint64_t i = 0; i < MAX_TASKS; i++) {
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

void scheduler_start() {
    if (run_queue == NULL) {
        // nothing to schedule
        for (;;) {
            asm volatile("hlt");
        }
    }

    current_task = run_queue;

    // find the first runnable task
    task_t* task = run_queue;
    for (u64 i = 0; i < MAX_TASKS; ++i) {
        if (task->state == TASK_RUNNABLE) {
            current_task = task;
            break;
        }

        task = task->next;
    }

    current_task->state = TASK_RUNNING;

    // no previous task, save dummy RSP location on stack
    u64 boot_rsp;
    x86_64_context_switch(&boot_rsp, current_task->rsp);

    // should never return here
    xassert(false, "");

    for (;;) {
        asm volatile("hlt");
    }
}

task_t* scheduler_current() {
    return current_task;
}

// find the next runnable task after current_task
static task_t* scheduler_next() {
    if (current_task == NULL) {
        return run_queue;
    }

    task_t* candidate = current_task->next;

    // search the circular list once
    task_t* start = candidate;

    do {
        if (candidate->state == TASK_RUNNABLE) {
            return candidate;
        }

        candidate = candidate->next;
    } while (candidate != start);

    // if nothing else is runnable keep running the current task
    if (current_task->state == TASK_RUNNING) {
        return current_task;
    }

    return NULL;
}

task_t* task_create(task_entry_t entry, void* arg) {
    if (entry == NULL) {
        return NULL;
    }

    task_t* task = NULL;

    for (u64 i = 0; i < MAX_TASKS; ++i) {
        if (tasks[i].state == TASK_UNUSED) {
            task = &tasks[i];
            break;
        }
    }

    if (task == NULL) {
        return NULL;
    }

    task->id = next_task_id++;
    task->state = TASK_RUNNABLE;

    task->stack = task_stacks[task - tasks];
    task->stack_size = TASK_STACK_SIZE;

    task->entry = entry;
    task->arg = arg;

    task_build_initial_stack(task);

    run_queue_add(task);

    return task;
}

void task_yield() {
    if (current_task == NULL) {
        return;
    }

    task_t* previous = current_task;
    task_t* next = scheduler_next();
    
    if (next == NULL || next == previous) {
        return;
    }

    previous->state = TASK_RUNNABLE;
    next->state = TASK_RUNNING;

    current_task = next;

    x86_64_context_switch(&previous->rsp, next->rsp);
}

__attribute__((noreturn)) void task_exit() {
    task_t* old = current_task;
    old->state = TASK_DEAD;
    
    task_t* next = scheduler_next();
    if (next == NULL || next == old) {
        // every task has exited
        xassert(false, "All tasks exited");
        for (;;) {
            asm volatile("hlt");
        }
    }

    next->state = TASK_RUNNING;
    current_task = next;
    
    // intentionally don't save old->rsp beacuse this task is dead and will never resume
    u64 unused_rsp;
    x86_64_context_switch(&unused_rsp, next->rsp);

    // should never return
    for (;;) {
        asm volatile("hlt");
    }
}

// C half of the task startup trampoline
void task_bootstrap_entry(task_t* task) {
    task_bootstrap(task);
}