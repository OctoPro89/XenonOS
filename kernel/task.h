#pragma once
#include <xlibc/xstdint.h>

#define MAX_TASKS 16
#define TASK_STACK_SIZE (16 * 1024)

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
    uint64_t id;
    task_state_t state;
    uint64_t rsp;
    uint8_t *stack;
    uint64_t stack_size;
    task_entry_t entry;
    void *arg;
    struct task *next;
} task_t;

void scheduler_init();
void scheduler_start();
task_t* scheduler_current();

task_t* task_create(task_entry_t entry, void* arg);
void task_yield();
__attribute__((noreturn)) void task_exit();