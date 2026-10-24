#pragma once

#include <xlibc/xstdint.h>
#include <kernel.h>

typedef enum {
    SMP_IPI_RESULT_OK = 0,
    SMP_IPI_RESULT_ERR_INVALID = -1,        // null handler, or a message that was never registered
    SMP_IPI_RESULT_ERR_FULL = -2,           // every message slot is taken
    SMP_IPI_RESULT_ERR_OFFLINE = -3,        // the target CPU is not online
    SMP_IPI_RESULT_ERR_UNREACHABLE = -4     // the interrupt controller cannot address the target CPU
} smp_ipi_result_t;

// One bit per message in a CPU's pending word
#define SMP_IPI_CPU_MAX_MESSAGES 32

/**
 * @struct message
 * @note An inter-processor interrupt asks another CPU to run a handler. The reason
 * for the interrupt is a `message`, and each message belongs to exactly one
 * subsystem, which registers it once during boot and keeps the returned value
 * to send it later.
 * Messages carry no payload and coalesce: a message sent to a CPU several
 * times before that CPU services its interrupt runs its handler once. A
 * subsystem that needs more than the fact of the interrupt must publish that
 * state itself before sending.
 * Handlers run on the receiving CPU in interrupt context with interrupts
 * masked. A handler must be short, must not take locks, and must never wait
 * for another CPU, since that CPU may be waiting for this one.
 */
struct message {
    u32 bit;
};

typedef void (*smp_ipi_handler_t)();

/**
 * @brief Set up delivery on the bootstrap CPU. Call after `irq::init`.
 * @return OK on success, negative error code on failure.
 */
__PRIVILEGED_CODE i32 smp_ipi_init();

/**
 * @brief Enable delivery on the calling AP. Call after `irq::init_ap`.
 * @return OK on success, negative error code on failure.
 */
__PRIVILEGED_CODE i32 smp_ipi_init_ap();

/**
 * @brief Bind `fn` to a new message. A subsystem calls this once, before any
 * CPU may send the message.
 * @param fn Handler run on the receiving CPU.
 * @param out Receives the message to pass to `send`.
 * @return OK on success, ERR_INVALID for a null argument, ERR_FULL when no slot is left.
 */
__PRIVILEGED_CODE i32 smp_ipi_register_message(smp_ipi_handler_t fn, struct message* out);

/**
 * @brief Interrupt one CPU with `m`. The calling CPU may target itself, the
 * handler then runs once it enables interrupts.
 * @param cpu Logical CPU id.
 * @return OK on success, ERR_INVALID for an unregistered message, ERR_OFFLINE when `cpu` is unknown or not online, ERR_UNREACHABLE when the interrupt controller cannot address it.
 */
__PRIVILEGED_CODE i32 smp_ipi_send(u32 cpu, struct message m);

/**
 * @brief Interrupt every online CPU except the caller with `m`.
 * @return The number of CPUs interrupted, which is the number of handler runs the caller may wait for.
 */
__PRIVILEGED_CODE u32 smp_ipi_send_all_but_self(struct message m);

/**
 * @brief Run the handler of every message pending on the calling CPU. Called
 * by the architecture's interrupt entry when the IPI vector fires.
 */
__PRIVILEGED_CODE void smp_ipi_dispatch();