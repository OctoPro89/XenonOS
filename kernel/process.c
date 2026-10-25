#include <process.h>
#include <task.h>
#include <errno.h>
#include <memory/heap.h>
#include <memory/pmm.h>
#include <memory/paging.h>
#include <memory/vmm.h>
#include <xlibc/string.h>
#include <xlibc/xassert.h>
#include <io/terminal_file.h>
#include <tty/terminal.h>
#include <arch/x86_64/sync/sync.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/io.h>
#include <unistd.h>
#include <errno.h>

static process_t* process_list;
static spinlock_t process_list_lock;
static pid_t next_pid = 1;


static u64 align_down_16(u64 value) {
    return value & ~0xFULL;
}

int process_setup_arguments(process_t* process, int argc, const char* const argv[]) {
    if (!process || argc < 0 || argc > PROCESS_MAX_ARGS) {
        return -EINVAL;
    }

    if (argc > 0 && !argv) {
        return -EINVAL;
    }

    const u64 stack_top = process->user_stack_top;
    const u64 stack_bottom = stack_top - (PROCESS_STACK_PAGES * PAGE_SIZE);

    /*
        need to access the child's user stack through its
        virtual addresses while writing to its physical pages
    
        since this function runs in the kernel, do not simply
        dereference child user virtual addresses: the child's
        address space may not be the currently active one
    
        use a temporary mapping-independent write helper below
     */

    u64 sp = stack_top;
    u64 argv_user[PROCESS_MAX_ARGS];

    // copy strings onto the stack, from last argument to first
    size_t total_string_bytes = 0;

    for (int i = argc - 1; i >= 0; --i) {
        if (!argv[i]) {
            return -EINVAL;
        }

        size_t len = strlen(argv[i]) + 1;

        if (len > PROCESS_MAX_ARG_BYTES - total_string_bytes) {
            return -E2BIG;
        }

        total_string_bytes += len;

        if (sp < stack_bottom || len > sp - stack_bottom) {
            return -E2BIG;
        }

        sp -= len;

        // record the child's USER VIRTUAL ADDRESS, argv[i] itself is a kernel pointer here
        argv_user[i] = sp;

        // copy the string into the child's address space
        size_t copied = 0;

        while (copied < len) {
            u64 page_va = (sp + copied) & ~(PAGE_SIZE - 1);
            size_t page_offset = (sp + copied) - page_va;
            size_t chunk = PAGE_SIZE - page_offset;

            if (chunk > len - copied) {
                chunk = len - copied;
            }

            paddr_t phys = vmm_virt_to_phys(process->space, page_va);

            if (!phys) {
                return -EFAULT;
            }

            void* dest = (u8*)phys_to_hhdm(phys) + page_offset;

            memcpy(dest, argv[i] + copied, chunk);

            copied += chunk;
        }
    }

    /*
     * reserve:
     *   argc             8 bytes
     *   argv[0..argc-1]  argc * 8 bytes
     *   argv[argc]       8 bytes (NULL)
     *
     * the whole initial stack area starts at a 16-byte aligned address
     */
    u64 table_words = (u64)argc + 2;
    u64 table_bytes = table_words * sizeof(u64);

    if (sp < stack_bottom || table_bytes > sp - stack_bottom) {
        return -E2BIG;
    }

    // place the table below the strings, then align its start down to a 16-byte boundary
    u64 table_start = align_down_16(sp - table_bytes);

    if (table_start < stack_bottom) {
        return -E2BIG;
    }

    sp = table_start;

    /*
     * construct a temporary kernel-side table
     *
     * layout:
     *   [0]         argc
     *   [1]         argv[0]
     *   ...
     *   [argc]      argv[argc - 1]
     *   [argc + 1]  NULL
     */
    u64 initial_stack[PROCESS_MAX_ARGS + 2];

    initial_stack[0] = (u64)argc;

    for (int i = 0; i < argc; ++i) {
        initial_stack[i + 1] = argv_user[i];
    }

    initial_stack[argc + 1] = 0;

    // write the table into the address spaec
    size_t table_copied = 0;

    while (table_copied < table_bytes) {
        u64 dest_va = sp + table_copied;
        u64 page_va = dest_va & ~(PAGE_SIZE - 1);
        size_t page_offset = dest_va - page_va;

        size_t chunk = PAGE_SIZE - page_offset;

        if (chunk > table_bytes - table_copied) {
            chunk = table_bytes - table_copied;
        }

        paddr_t phys = vmm_virt_to_phys(process->space, page_va);

        if (!phys) {
            return -EFAULT;
        }

        void* dest = (u8*)phys_to_hhdm(phys) + page_offset;

        memcpy(dest, (const u8*)initial_stack + table_copied, chunk);

        table_copied += chunk;
    }

    // rsp points to argc argv begins at rsp+ 8
    process->user_stack_top = stack_top;
    process->initial_user_rsp = sp;

    return 0;
}

static void process_register(process_t* process) {
    u64 flags = 0;
    spin_lock_irqsave(&process_list_lock, &flags);
    process->next = process_list;
    process_list = process;
    spin_unlock_irqrestore(&process_list_lock, flags);
}

static void process_unregister(process_t* process) {
    u64 flags = 0;
    spin_lock_irqsave(&process_list_lock, &flags);
    process_t** current = &process_list;
    while (*current != NULL) {
        if (*current == process) {
            *current = process->next;
            break;
        }

        current = &(*current)->next; // this shit is cursed
    }
    spin_unlock_irqrestore(&process_list_lock, flags);
}

process_t* process_create() {
    process_t* process = kmalloc(sizeof(process_t));
    if (!process) {
        return NULL;
    }

    memset(process, 0, sizeof(process_t));

    wait_queue_init(&process->waiters);
    spin_lock_init(&process->lock);

    process->pid = __atomic_fetch_add(&next_pid, 1, __ATOMIC_RELAXED);

    process->space = vmm_create_space();

    if (!process->space) {
        kfree(process);
        return NULL;
    }

    process->space->user_mode = true;
    process->state = PROCESS_RUNNING; // TODO: I assume this is fine

    fd_table_init(&process->fd_table);

    process_register(process);

    return process;
}

void process_destroy(process_t* process) {
    if (!process) {
        return;
    }

    process_unregister(process);
    
    fd_table_destroy(&process->fd_table);
    vmm_destroy_space(process->space);
    kfree(process);
}

int process_start(process_t* process) {
    if (!process) {
        return -EINVAL;
    }

    task_t* task = task_create(process, user_process_task_entry, process);
    if (!task) {
        return -ENOMEM;
    }

    process->main_task = task;
    return 0;
}

void process_exit(process_t* process, int exit_code) {
    if (!process) {
        return;
    }

    u64 flags = 0;
    spin_lock_irqsave(&process->lock, &flags);

    process->exit_code = exit_code;
    process->state = PROCESS_ZOMBIE;

    wait_queue_wake_all_locked(&process->waiters);

    spin_unlock_irqrestore(&process->lock, flags);
}

int process_wait(process_t* process) {
    for (;;) {
        u64 flags = 0;
        spin_lock_irqsave(&process->lock, &flags);

        if (process->state == PROCESS_ZOMBIE) {
            int status = process->exit_code;
            spin_unlock_irqrestore(&process->lock, flags);

            return status;
        }

        flags = task_wait(&process->waiters, &process->lock, flags);

        // task_wait() returns with the lock held and ints disabled
        spin_unlock_irqrestore(&process->lock, flags);
    }
}

process_t* process_current() {
    task_t* task = scheduler_current();
    if (!task) {
        return NULL;
    }

    return task->process;
}

process_t* process_find(u64 pid) {
    u64 flags = 0;
    process_t* result = NULL;

    spin_lock_irqsave(&process_list_lock, &flags);
    process_t* current = process_list;

    while (current != NULL) {
        if (current->pid == pid) {
            result = current;
            break;
        }

        current = current->next;
    }

    spin_unlock_irqrestore(&process_list_lock, flags);

    return result;
}

void process_reap(process_t* process) {
    if (!process) {
        return;
    }

    xassert(process->state == PROCESS_ZOMBIE, "Attempting to reap non-zombie process");

    if (process->main_task) {
        task_reap(process->main_task);
        process->main_task = NULL;
    }

    process_destroy(process);
}

int process_setup_stdio(process_t* process, terminal_t* terminal) {
    if (!process || !terminal) {
        return -EINVAL;
    }

    file_t* stdin_file  = terminal_file_create(terminal);
    file_t* stdout_file = terminal_file_create(terminal);
    file_t* stderr_file = terminal_file_create(terminal);

    if (!stdin_file || !stdout_file || !stderr_file) {
        if (stdin_file)  file_put(stdin_file);
        if (stdout_file) file_put(stdout_file);
        if (stderr_file) file_put(stderr_file);

        return -ENOMEM;
    }

    // this process must have an empty FD table, standard descriptors are in 0, 1, and 2
    fd_t in_fd = fd_table_alloc(&process->fd_table, stdin_file);

    fd_t out_fd = FD_INVALID;
    fd_t err_fd = FD_INVALID;

    if (in_fd == 0) {
        out_fd = fd_table_alloc(&process->fd_table, stdout_file);
    }

    if (out_fd == 1) {
        err_fd = fd_table_alloc(&process->fd_table, stderr_file);
    }

    // drop creator references
    file_put(stdin_file);
    file_put(stdout_file);
    file_put(stderr_file);

    if (in_fd == 0 && out_fd == 1 && err_fd == 2) {
        return 0;
    }

    // roll back any descriptors that were successfully allocated
    if (err_fd >= 0) {
        fd_table_close(&process->fd_table, err_fd);
    }
    if (out_fd >= 0) {
        fd_table_close(&process->fd_table, out_fd);
    }
    if (in_fd >= 0) {
        fd_table_close(&process->fd_table, in_fd);
    }

    return -EMFILE;
}

extern void ASMCALL enter_user_mode(u64 entry, u64 stack) __attribute__((noreturn));

void user_process_task_entry(void* arg) {
    process_t* process = arg;

    vmm_switch(process->space);

    /*
     * initial_user_rsp points to:
     *   [rsp]       = argc
     *   [rsp + 8]   = argv[0]
     *   ...
     *   [rsp + 8 * (argc + 1)] = NULL
     *
     * process_setup_arguments() has already aligned RSP to 16 bytes
     */
    u64 user_rsp = process->initial_user_rsp;

    enter_user_mode(process->entry, user_rsp);

    __builtin_unreachable();
}

void process_handle_user_exception(struct regs* regs) {
    task_t* task = scheduler_current();
    process_t* process = task ? task->process : NULL;

    // this handler is only valid for a genuine user-mode exception
    if (!regs || (regs->cs & 3) != 3 || !process) {
        serial_write_str("\r\nFATAL: user exception handler entered without a valid user process\r\n");

        for (;;) {
            asm volatile("cli; hlt");
        }
    }

    serial_write_str("\r\nProcess exception\r\n");

    serial_write_str("PID: ");
    serial_write_hex((u64)process->pid);

    serial_write_str(" Vector: ");
    serial_write_hex(regs->int_no);

    serial_write_str(" RIP: ");
    serial_write_hex(regs->rip);

    serial_write_str(" Error: ");
    serial_write_hex(regs->err_code);

    serial_write_str("\r\n");

    // Publish the process's exit status and wake process_wait(), EFAULT is used here as a generic abnormal-termination status
    process_exit(process, -EFAULT);

    // never return to the faulting instruction, the scheduler marks this task dead and switches away from it
    task_exit();
}