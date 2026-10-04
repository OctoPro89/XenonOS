#include <task.h>

#include <arch/x86_64/sync/critical_section.h>
#include <arch/x86_64/sync/sync.h>
#include <arch/x86_64/irq.h>
#include <time/time.h>
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
static b8 need_reschedule;

static void idle_task(void *arg) {
    (void)arg;

    for (;;) {
        asm volatile("hlt");
    }
}

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

task_t* scheduler_current() {
    return current_task;
}

task_t* task_create(task_entry_t entry, void* arg) {
    if (entry == NULL)
        return NULL;

    u64 flags = irq_save();

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
    task->name = "TASK";

    task_build_initial_stack(task);
    run_queue_add(task);

    irq_restore(flags);

    return task;
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
        tasks[i].wait_next = NULL;
        tasks[i].waiting_on = NULL;
        tasks[i].wake_deadline_ns = 0;
        tasks[i].is_idle = false;
        tasks[i].name = "TASK";
    }

    // create idle as task 0
    task_t* idle = task_create(idle_task, NULL);
    xassert(idle != NULL, "Failed to create idle task");
    idle->is_idle = true;
}

void scheduler_start() {
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

/*
 * @brief Find another runnable task after the current task.
 *
 * The current task is returned only when no other runnable task exists
 * and the current task is still running.
 *
 * This distinction matters for task_exit(): a dead task must never
 * be selected as the fallback task.
 */
static task_t* scheduler_next() {
    if (current_task == NULL) {
        return run_queue;
    }

    task_t* candidate = current_task->next;
    task_t* start = candidate;

    // first preference: normal runnable tasks
    do {
        if (candidate->state == TASK_RUNNABLE && !candidate->is_idle) {
            return candidate;
        }

        candidate = candidate->next;
    } while (candidate != start);

    // if the current normal task can continue, keep running it
    if (current_task->state == TASK_RUNNABLE && !current_task->is_idle) {
        return current_task;
    }

    // if the idle task is running and nobody else can run keep the idle task
    if (current_task->is_idle && current_task->state == TASK_RUNNING) {
        return current_task;
    }

    // otherwise find the idle task
    candidate = run_queue;
    start = candidate;

    do {
        if (candidate->state == TASK_RUNNABLE || candidate->state == TASK_RUNNING && candidate->is_idle) {
            return candidate;
        }

        candidate = candidate->next;
    } while (candidate != start);

    return NULL;
}

u64 scheduler_handle_interrupt(u64 interrupted_rsp, u8 vector) {
    if (current_task == NULL) {
        return interrupted_rsp;
    }

    b8 scheduler_event = vector == timer_vector || vector == TASK_YIELD_VECTOR || vector == TASK_BLOCK_VECTOR || vector == TASK_SLEEP_VECTOR || vector == TASK_EXIT_VECTOR;

    // a normal interrupt can still trigger a reschedule if it woke somebody
    if (!scheduler_event && !need_reschedule) {
        return interrupted_rsp;
    }

    task_t* previous = current_task;

    // save exactly where this task was interrupted
    previous->rsp = interrupted_rsp;

    /*
     * BLOCK/SLEEP software interrupts are generated with IF=0
     * because atomic state/queue changes were needed 
     *
     * make the task resume later with interrupts enabled
     */
    if (vector == TASK_BLOCK_VECTOR || vector == TASK_SLEEP_VECTOR) {
        ((struct regs *)interrupted_rsp)->rflags |= X86_EFLAGS_IF;
    }

    if (vector == TASK_EXIT_VECTOR) {
        previous->state = TASK_DEAD;
    }
    else if (vector == TASK_YIELD_VECTOR) {
        previous->state = TASK_RUNNABLE;
    }

    task_t* next = scheduler_next();

    if (next == NULL) {
        xassert(false, "scheduler has no runnable task");

        for (;;) {
            asm volatile("hlt");
        }
    }

    need_reschedule = false;

    if (next == previous) {
        previous->state = TASK_RUNNING;
        return previous->rsp;
    }

    if (previous->state == TASK_RUNNING) {
        previous->state = TASK_RUNNABLE;
    }

    next->state = TASK_RUNNING;
    current_task = next;

    return next->rsp;
}

void scheduler_wake_sleepers(u64 now_ns) {
    for (u64 i = 0; i < MAX_TASKS; ++i) {
        task_t* task = &tasks[i];

        if (task->state != TASK_SLEEPING) {
            continue;
        }

        if (now_ns >= task->wake_deadline_ns) {
            task->wake_deadline_ns = 0;
            task->state = TASK_RUNNABLE;
            need_reschedule = true;
        }
    }
}

void scheduler_request_reschedule() {
    need_reschedule = true;
}

void task_yield() {
    asm volatile(
        "int $0xF0"
        :
        :
        : "memory", "cc"
    );
}

__attribute__((noreturn))
void task_exit() {
    asm volatile(
        "int $0xF1"
        :
        :
        : "memory", "cc"
    );

    __builtin_unreachable();
}

void task_sleep_ns(u64 ns) {
    if (ns == 0) {
        task_yield();
        return;
    }

    task_t* task = current_task;
    xassert(task != NULL, "sleep with no current task");

    u64 flags = irq_save();
    xassert(irq_was_enabled(flags), "task_sleep_ns with interrupts disabled");

    u64 now = ktimer_get_system_time_in_nanoseconds();

    task->wake_deadline_ns = now + ns;
    task->state = TASK_SLEEPING;

    asm volatile(
        "int $0xF3"
        :
        :
        : "memory", "cc"
    );

    irq_restore(flags);
}

void task_sleep_ms(u64 ms) {
    task_sleep_ns(ms * 1000000ULL);
}

// C half of the startup trampoline
void task_bootstrap_entry(task_t* task) {
    task_bootstrap(task);
}

static void wait_queue_add_locked(wait_queue_t* queue, task_t* task) {
    task->wait_next = NULL;
    task->waiting_on = queue;

    if (queue->tail == NULL) {
        queue->head = task;
        queue->tail = task;
        return;
    }

    queue->tail->wait_next = task;
    queue->tail = task;
}

static task_t* wait_queue_pop_locked(wait_queue_t* queue) {
    task_t* task = queue->head;

    if (task == NULL) {
        return NULL;
    }

    queue->head = task->wait_next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    task->wait_next = NULL;
    task->waiting_on = NULL;

    return task;
}

void task_block_on(wait_queue_t* queue) {
    u64 flags = irq_save();

    // sleeping / blocking with interrupts already disabled is currently not a supported public API
    xassert(irq_was_enabled(flags), "task_block_on with interrupts disabled");

    task_block_current_locked(queue);
    irq_restore(flags);
}

void task_block_current_locked(wait_queue_t* queue) {
    task_t* task = current_task;

    xassert(task != NULL, "blocking with no current task");
    xassert(queue != NULL, "blocking on NULL wait queue");

    wait_queue_add_locked(queue, task);

    task->state = TASK_BLOCKED;

    /*
     * entered here with interrupts disabled,
     * the scheduler's BLOCK handler will force IF=1 in the
     * saved frame before this task eventually resumes
     */
    asm volatile(
        "int $0xF2"
        :
        :
        : "memory", "cc"
    );
}

u64 task_wait(wait_queue_t* queue, spinlock_t* lock, u64 flags) {
    task_t* task = scheduler_current();

    xassert(task != NULL, "task_wait with no current task");
    xassert(queue != NULL, "task_wait on NULL wait queue");
    xassert(lock != NULL, "task_wait with NULL lock");

    /*
     * at this point:
     *   interrupts are disabled and lock is held
     * add ourselves to the wait queue and mark ourselves blocked before releasing the lock
     */
    wait_queue_add_locked(queue, task);
    task->state = TASK_BLOCKED;

    /*
     * release the lock and restore the previous IRQ state
     * NOTE: this must happen AFTER adding ourselves to the wait queue
     */
    spin_unlock_irqrestore(lock, flags);

    /*
     * block, the scheduler will switch away from us
     * don't return here until somebody wakes us and the scheduler chooses us again.
     */
    asm volatile(
        "int $0xF2"
        :
        :
        : "memory", "cc"
    );

    // been reawakened, reacquire the lock and return the new IRQ state
    u64 new_flags;
    spin_lock_irqsave(lock, &new_flags);

    return new_flags;
}

void wait_queue_init(wait_queue_t* queue) {
    queue->head = NULL;
    queue->tail = NULL;
}

void wait_queue_wake_one_locked(wait_queue_t* queue) {
    task_t* task = wait_queue_pop_locked(queue);

    if (task == NULL) {
        return;
    }

    task->state = TASK_RUNNABLE;
    need_reschedule = true;
}

void wait_queue_wake_all_locked(wait_queue_t* queue) {
    while (queue->head != NULL) {
        task_t* task = wait_queue_pop_locked(queue);

        task->state = TASK_RUNNABLE;
        need_reschedule = true;
    }
}

void wait_queue_wake_one(wait_queue_t* queue) {
    u64 flags = irq_save();
    wait_queue_wake_one_locked(queue);

    irq_restore(flags);
}

void wait_queue_wake_all(wait_queue_t* queue) {
    u64 flags = irq_save();
    wait_queue_wake_all_locked(queue);

    irq_restore(flags);
}