#include <process.h>
#include <task.h>
#include <errno.h>
#include <xlibc/stdlib.h>
#include <io/console_file.h>
#include <tty/terminal.h>

static int process_setup_stdio(process_t* process) {
    terminal_t* terminal;
    file_t* stdin_file;
    file_t* stdout_file;
    file_t* stderr_file;
    
    terminal = console_terminal();

    stdin_file = terminal_file_create(terminal);
    stdout_file = terminal_file_create(terminal);
    stderr_file = terminal_file_create(terminal);

    if (!stdin_file || !stdout_file || !stderr_file) {
        if (stdin_file) { file_put(stdin_file); }
        if (stdout_file) { file_put(stdout_file); }
        if (stderr_file) { file_put(stderr_file); }
        return -ENOMEM;
    }

    // the FD table takes a reference
    if (fd_table_alloc(&process->fd_table, stdin_file) != 0) {
        file_put(stdin_file);
        file_put(stdout_file);
        file_put(stderr_file);
        return -EMFILE;
    }


    if (fd_table_alloc(&process->fd_table, stdout_file) != 1) {
        fd_table_close(&process->fd_table, 0);
        file_put(stdout_file);
        file_put(stderr_file);
        return -EMFILE;
    }


    if (fd_table_alloc(&process->fd_table, stderr_file) != 2) {
        fd_table_close(&process->fd_table, 0);
        fd_table_close(&process->fd_table, 1);
        file_put(stderr_file);
        return -EMFILE;
    }

    // drop the creator references, the FD table now owns them
    file_put(stdin_file);
    file_put(stdout_file);
    file_put(stderr_file);

    return 0;
}

static u64 next_pid = 1;

process_t* process_create() {
    process_t* process = kmalloc(sizeof(process_t));
    if (!process) {
        return NULL;
    }

    process->pid = __atomic_fetch_add(&next_pid, 1, __ATOMIC_RELAXED);

    process->space = vmm_create_space();

    if (!process->space) {
        kfree(process);
        return NULL;
    }

    process->space->user_mode = true;

    fd_table_init(&process->fd_table);

    if (process_setup_stdio(process) < 0) {
        fd_table_destroy(&process->fd_table);
        vmm_destroy_space(process->space);
        kfree(process);
        return NULL;
    }

    return process;
}

void process_destroy(process_t* process) {
    if (!process) {
        return;
    }
    
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

        task_wait(&process->waiters, &process->lock, flags);

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

extern void ASMCALL enter_user_mode(u64 entry, u64 stack) __attribute__((noreturn));

void user_process_task_entry(void* arg) {
    process_t* process = arg;
    vmm_switch(process->space);
    enter_user_mode(process->entry, process->user_stack_top & ~0xFULL);

    __builtin_unreachable();
}