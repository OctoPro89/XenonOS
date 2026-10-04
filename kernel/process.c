#include <process.h>
#include <task.h>
#include <errno.h>
#include <xlibc/stdlib.h>
#include <io/console_file.h>

static int process_setup_stdio(process_t* process) {
    file_t* stdin_file = console_file_create();
    file_t* stdout_file = console_file_create();
    file_t* stderr_file = console_file_create();

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

        // TODO: destroy space
        kfree(process);
        return NULL;
    }

    return process;
}

void process_destroy(process_t* process) {
    if (!process) {
        return;
    }
    
    // TODO: destroy address space

    fd_table_destroy(&process->fd_table);

    kfree(process);
}

process_t* process_current() {
    task_t* task = scheduler_current();
    if (!task) {
        return NULL;
    }

    return task->process;
}