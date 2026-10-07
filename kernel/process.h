#pragma once

#include <io/fd_table.h>
#include <xlibc/xstdint.h>
#include <memory/vmm.h>
#include <arch/x86_64/sync/sync.h>

typedef struct process process_t;

typedef u64 pid_t;

typedef enum {
    PROCESS_RUNNING = 0,
    PROCESS_ZOMBIE
} process_state_t;

struct process {
    pid_t pid;
    vmm_space_t* space;
    fd_table_t fd_table;

    spinlock_t lock;
    wait_queue_t waiters;

    process_state_t state;
    int exit_code;

    u64 entry;
    u64 user_stack_top;

    task_t* main_task;

    process_t* parent;
    process_t* next;
};

process_t* process_create();
void process_destroy(process_t* process);
int process_start(process_t* process);
void process_exit(process_t* process, int exit_code);
int process_wait(process_t* process);

process_t* process_current();

process_t* process_find(u64 pid);
void process_reap(process_t* process);

void user_process_task_entry(void* arg);