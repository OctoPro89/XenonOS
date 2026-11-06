#pragma once

#include <xlibc/xstdint.h>

#define ATOMIC_RELAXED __ATOMIC_RELAXED
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

#define ATOMIC_TYPE(name, type) \
typedef struct { \
    type value; \
} atomic_##name

ATOMIC_TYPE(u8, u8);
ATOMIC_TYPE(u16, u16);
ATOMIC_TYPE(u32, u32);
ATOMIC_TYPE(u64, u64);

ATOMIC_TYPE(i8, i8);
ATOMIC_TYPE(i16, i16);
ATOMIC_TYPE(i32, i32);
ATOMIC_TYPE(i64, i64);

ATOMIC_TYPE(b8, b8);

#undef ATOMIC_TYPE

#define ATOMIC_INIT(v) { .value = (v) }

#define ATOMIC_DECLARE(name, type) \
void atomic_##name##_init(atomic_##name* a, type value); \
type atomic_##name##_load_relaxed(const atomic_##name* a); \
type atomic_##name##_load_acquire(const atomic_##name* a); \
void atomic_##name##_store_relaxed(atomic_##name* a, type value); \
void atomic_##name##_store_release(atomic_##name* a, type value); \
type atomic_##name##_exchange_relaxed(atomic_##name* a, type v); \
type atomic_##name##_exchange_acquire(atomic_##name* a, type v); \
type atomic_##name##_exchange_release(atomic_##name* a, type v); \
type atomic_##name##_exchange_acq_rel(atomic_##name* a, type v); \
b8 atomic_##name##_cmpxchg_weak_relaxed(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_weak_acquire(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_weak_release(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_weak_acq_rel(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_strong_relaxed(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_strong_acquire(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_strong_release(atomic_##name* a, type* expected, type desired); \
b8 atomic_##name##_cmpxchg_strong_acq_rel(atomic_##name* a, type* expected, type desired);

#define ATOMIC_DECLARE_ARITH(name, type) \
type atomic_##name##_fetch_add_relaxed(atomic_##name* a, type v); \
type atomic_##name##_fetch_add_acquire(atomic_##name* a, type v); \
type atomic_##name##_fetch_add_release(atomic_##name* a, type v); \
type atomic_##name##_fetch_add_acq_rel(atomic_##name* a, type v); \
type atomic_##name##_fetch_sub_relaxed(atomic_##name* a, type v); \
type atomic_##name##_fetch_sub_acquire(atomic_##name* a, type v); \
type atomic_##name##_fetch_sub_release(atomic_##name* a, type v); \
type atomic_##name##_fetch_sub_acq_rel(atomic_##name* a, type v); \
type atomic_##name##_fetch_and_relaxed(atomic_##name* a, type v); \
type atomic_##name##_fetch_and_acquire(atomic_##name* a, type v); \
type atomic_##name##_fetch_and_release(atomic_##name* a, type v); \
type atomic_##name##_fetch_and_acq_rel(atomic_##name* a, type v); \
type atomic_##name##_fetch_or_relaxed(atomic_##name* a, type v);  \
type atomic_##name##_fetch_or_acquire(atomic_##name* a, type v);  \
type atomic_##name##_fetch_or_release(atomic_##name* a, type v);  \
type atomic_##name##_fetch_or_acq_rel(atomic_##name* a, type v);  \
type atomic_##name##_fetch_xor_relaxed(atomic_##name* a, type v); \
type atomic_##name##_fetch_xor_acquire(atomic_##name* a, type v); \
type atomic_##name##_fetch_xor_release(atomic_##name* a, type v); \
type atomic_##name##_fetch_xor_acq_rel(atomic_##name* a, type v);

ATOMIC_DECLARE(u8, u8);
ATOMIC_DECLARE(u16, u16);
ATOMIC_DECLARE(u32, u32);
ATOMIC_DECLARE(u64, u64);

ATOMIC_DECLARE(i8, i8);
ATOMIC_DECLARE(i16, i16);
ATOMIC_DECLARE(i32, i32);
ATOMIC_DECLARE(i64, i64);

ATOMIC_DECLARE(b8, b8);

ATOMIC_DECLARE_ARITH(u8, u8);
ATOMIC_DECLARE_ARITH(u16, u16);
ATOMIC_DECLARE_ARITH(u32, u32);
ATOMIC_DECLARE_ARITH(u64, u64);

ATOMIC_DECLARE_ARITH(i8, i8);
ATOMIC_DECLARE_ARITH(i16, i16);
ATOMIC_DECLARE_ARITH(i32, i32);
ATOMIC_DECLARE_ARITH(i64, i64);

#undef ATOMIC_DECLARE
#undef ATOMIC_DECLARE_ARITH

void atomic_fence_acquire(void);
void atomic_fence_release(void);
void atomic_fence_seq_cst(void);