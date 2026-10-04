#pragma once

#include <xlibc/xstdint.h>
#include <task.h>

typedef struct spinlock {
    volatile u32 locked;
} spinlock_t;

void spin_lock_init(spinlock_t* lock);
void spin_lock_irqsave(spinlock_t* lock, u64* flags);
void spin_unlock_irqrestore(spinlock_t* lock, u64 flags);

typedef struct {
    task_t* owner;
    wait_queue_t waiters;
} mutex_t;

void mutex_init(mutex_t* mutex);
void mutex_lock(mutex_t* mutex);
void mutex_unlock(mutex_t* mutex);