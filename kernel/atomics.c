#include <atomics.h>

#define ATOMIC_IMPLEMENT(name, type) \
void atomic_##name##_init(atomic_##name* a, type v) { \
    __atomic_store_n(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_load_relaxed(const atomic_##name* a) { \
    return __atomic_load_n(&a->value, ATOMIC_RELAXED); \
} \
type atomic_##name##_load_acquire(const atomic_##name* a) { \
    return __atomic_load_n(&a->value, ATOMIC_ACQUIRE); \
} \
void atomic_##name##_store_relaxed(atomic_##name* a, type v) { \
    __atomic_store_n(&a->value, v, ATOMIC_RELAXED); \
} \
void atomic_##name##_store_release(atomic_##name* a, type v) { \
    __atomic_store_n(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_exchange_relaxed(atomic_##name* a, type v) { \
    return __atomic_exchange_n(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_exchange_acquire(atomic_##name* a, type v) { \
    return __atomic_exchange_n(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_exchange_release(atomic_##name* a, type v) { \
    return __atomic_exchange_n(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_exchange_acq_rel(atomic_##name* a, type v) { \
    return __atomic_exchange_n(&a->value, v, ATOMIC_ACQ_REL); \
} \
b8 atomic_##name##_cmpxchg_weak_relaxed(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 1, ATOMIC_RELAXED, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_weak_acquire(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 1, ATOMIC_ACQUIRE, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_weak_release(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 1, ATOMIC_RELEASE, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_weak_acq_rel(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 1, ATOMIC_ACQ_REL, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_strong_relaxed(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 0, ATOMIC_RELAXED, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_strong_acquire(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 0, ATOMIC_ACQUIRE, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_strong_release(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 0, ATOMIC_RELEASE, ATOMIC_RELAXED); \
} \
b8 atomic_##name##_cmpxchg_strong_acq_rel(atomic_##name* a, type* expected, type desired) { \
    return __atomic_compare_exchange_n(&a->value, expected, desired, 0, ATOMIC_ACQ_REL, ATOMIC_RELAXED); \
} \

#define ATOMIC_IMPLEMENT_ARITH(name, type) \
type atomic_##name##_fetch_add_relaxed(atomic_##name* a, type v) { \
    return __atomic_fetch_add(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_fetch_add_acquire(atomic_##name* a, type v) { \
    return __atomic_fetch_add(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_fetch_add_release(atomic_##name* a, type v) { \
    return __atomic_fetch_add(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_fetch_add_acq_rel(atomic_##name* a, type v) { \
    return __atomic_fetch_add(&a->value, v, ATOMIC_ACQ_REL); \
} \
type atomic_##name##_fetch_sub_relaxed(atomic_##name* a, type v) { \
    return __atomic_fetch_sub(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_fetch_sub_acquire(atomic_##name* a, type v) { \
    return __atomic_fetch_sub(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_fetch_sub_release(atomic_##name* a, type v) { \
    return __atomic_fetch_sub(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_fetch_sub_acq_rel(atomic_##name* a, type v) { \
    return __atomic_fetch_sub(&a->value, v, ATOMIC_ACQ_REL); \
} \
type atomic_##name##_fetch_and_relaxed(atomic_##name* a, type v) { \
    return __atomic_fetch_and(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_fetch_and_acquire(atomic_##name* a, type v) { \
    return __atomic_fetch_and(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_fetch_and_release(atomic_##name* a, type v) { \
    return __atomic_fetch_and(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_fetch_and_acq_rel(atomic_##name* a, type v) { \
    return __atomic_fetch_and(&a->value, v, ATOMIC_ACQ_REL); \
} \
type atomic_##name##_fetch_or_relaxed(atomic_##name* a, type v) { \
    return __atomic_fetch_or(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_fetch_or_acquire(atomic_##name* a, type v) { \
    return __atomic_fetch_or(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_fetch_or_release(atomic_##name* a, type v) { \
    return __atomic_fetch_or(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_fetch_or_acq_rel(atomic_##name* a, type v) { \
    return __atomic_fetch_or(&a->value, v, ATOMIC_ACQ_REL); \
} \
type atomic_##name##_fetch_xor_relaxed(atomic_##name* a, type v) { \
    return __atomic_fetch_xor(&a->value, v, ATOMIC_RELAXED); \
} \
type atomic_##name##_fetch_xor_acquire(atomic_##name* a, type v) { \
    return __atomic_fetch_xor(&a->value, v, ATOMIC_ACQUIRE); \
} \
type atomic_##name##_fetch_xor_release(atomic_##name* a, type v) { \
    return __atomic_fetch_xor(&a->value, v, ATOMIC_RELEASE); \
} \
type atomic_##name##_fetch_xor_acq_rel(atomic_##name* a, type v) { \
    return __atomic_fetch_xor(&a->value, v, ATOMIC_ACQ_REL); \
}

ATOMIC_IMPLEMENT(u8, u8)
ATOMIC_IMPLEMENT(u16, u16)
ATOMIC_IMPLEMENT(u32, u32)
ATOMIC_IMPLEMENT(u64, u64)

ATOMIC_IMPLEMENT(i8, i8)
ATOMIC_IMPLEMENT(i16, i16)
ATOMIC_IMPLEMENT(i32, i32)
ATOMIC_IMPLEMENT(i64, i64)

ATOMIC_IMPLEMENT(b8, b8)


ATOMIC_IMPLEMENT_ARITH(u8, u8)
ATOMIC_IMPLEMENT_ARITH(u16, u16)
ATOMIC_IMPLEMENT_ARITH(u32, u32)
ATOMIC_IMPLEMENT_ARITH(u64, u64)

ATOMIC_IMPLEMENT_ARITH(i8, i8)
ATOMIC_IMPLEMENT_ARITH(i16, i16)
ATOMIC_IMPLEMENT_ARITH(i32, i32)
ATOMIC_IMPLEMENT_ARITH(i64, i64)

#undef ATOMIC_IMPLEMENT
#undef ATOMIC_IMPLEMENT_ARITH

void atomic_fence_acquire(void) { __atomic_thread_fence(ATOMIC_ACQUIRE); }
void atomic_fence_release(void) { __atomic_thread_fence(ATOMIC_RELEASE); }
void atomic_fence_seq_cst(void) { __atomic_thread_fence(ATOMIC_SEQ_CST); }