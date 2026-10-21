#pragma once

#include <io/fd_table.h>
#include <xlibc/xstdint.h>
#include <memory/vmm.h>
#include <arch/x86_64/sync/sync.h>
#include <tty/terminal.h>

#define PROCESS_CWD_MAX 256

struct regs;
typedef struct process process_t;

typedef u64 pid_t;

typedef enum {
    PROCESS_RUNNING = 0,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct window window_t;

struct process {
    pid_t pid; // TODO: this may need to be marked volatile
    vmm_space_t* space;
    fd_table_t fd_table;

    // userspace heap
    vaddr_t heap_start;
    vaddr_t heap_break;
    vaddr_t heap_limit;

    spinlock_t lock;
    wait_queue_t waiters;

    process_state_t state;
    int exit_code;

    u64 entry;
    u64 user_stack_top;

    task_t* main_task;
    window_t* window; // for now processes only have one window
    char cwd[PROCESS_CWD_MAX]; // current working directory

    process_t* parent;
    process_t* next;
};

process_t* process_create();

/**
 * @brief Cleans up a process but NOT its `main_task`. Does not require zombie
 * @note Should be used for cleaning up processes that failed to start or internally
 */
void process_destroy(process_t* process);
int process_start(process_t* process);
void process_exit(process_t* process, int exit_code);
int process_wait(process_t* process);

process_t* process_current();

process_t* process_find(u64 pid);

/**
 * @brief Cleans up a process and internally calls process_destry
 * @note Should only be used on successfully exited processes
 */
void process_reap(process_t* process);

int process_setup_stdio(process_t* process, terminal_t* terminal);

void user_process_task_entry(void* arg);

void process_handle_user_exception(struct regs* regs) __attribute__((noreturn));