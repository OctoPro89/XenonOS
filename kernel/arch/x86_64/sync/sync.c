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
    mutex->owner = NULL;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(mutex_t* mutex) {
    task_t* current = scheduler_current();
    xassert(current != NULL, "mutex_lock with no current task");

    for (;;) {
        u64 flags = irq_save();

        if (mutex->owner == NULL) {
            mutex->owner = current;
            irq_restore(flags);
            return;
        }

        /*
         * hold interrupts disabled while adding ourselves to the wait queue and changing our state.
         * NOTE: task_block_current_locked() does the scheduler trap
         */
        task_block_current_locked(&mutex->waiters);

        // resume here after somebody wakes us
        irq_restore(flags);
    }
}

void mutex_unlock(mutex_t* mutex) {
    task_t* current = scheduler_current();
    xassert(mutex->owner == current, "mutex_unlock by non-owner");

    u64 flags = irq_save();
    mutex->owner = NULL;

    // Wake one waiter, the waiter will contend for the mutex when it resumes
    wait_queue_wake_one_locked(&mutex->waiters);

    irq_restore(flags);
}