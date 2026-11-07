#include <arch/x86_64/sync/sync.h>
#include <arch/x86_64/sync/critical_section.h>
#include <xlibc/xstddef.h>
#include <xlibc/xassert.h>

void spin_lock_init(spinlock_t* lock) {
    lock->locked = 0;
}

void spin_lock_irqsave(spinlock_t* lock, u64* flags) {
    *flags = irq_save();

    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        asm volatile("pause");
    }
}

void spin_unlock_irqrestore(spinlock_t* lock, u64 flags) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
    irq_restore(flags);
}

void mutex_init(mutex_t* mutex) {
    spin_lock_init(&mutex->guard);
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_init(mutex_t* mutex) {
    spin_lock_init(&mutex->guard);
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(mutex_t* mutex) {
    task_t* current = scheduler_current();

    xassert(current != NULL, "mutex_lock with no current task");

    for (;;) {
        u64 flags;

        spin_lock_irqsave(&mutex->guard, &flags);

        if (mutex->owner == NULL) {
            mutex->owner = current;

            spin_unlock_irqrestore(&mutex->guard, flags);
            return;
        }

        xassert(mutex->owner != current, "Recursive mutex acquisition");

        // atomically enqueue ourselves, release the guard, and block until awakened. task_wait() returns with the guard held and interrupts disabled
        flags = task_wait(&mutex->waiters, &mutex->guard, flags);

        spin_unlock_irqrestore(&mutex->guard, flags);
    }
}

void mutex_unlock(mutex_t* mutex) {
    task_t* current = scheduler_current();

    u64 flags;
    spin_lock_irqsave(&mutex->guard, &flags);

    xassert(mutex->owner == current, "mutex_unlock by non-owner");

    mutex->owner = NULL;

    wait_queue_wake_one_locked(&mutex->waiters);
    spin_unlock_irqrestore(&mutex->guard, flags);
}