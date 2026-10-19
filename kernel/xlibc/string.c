#include <xlibc/string.h>
#include <xlibc/xstddef.h>
#include <memory/heap.h>
#include <limits.h>

void* memset(void* dest, int c, size_t n) {
    u8* d = (u8*)dest;
    
    // If small, do bytewise
    while (n && ((uintptr_t)d & 7)) {
        *d++ = (u8)c;
        n--;
    }

    // Fill qwords in bulk
    u64 pattern = (u8)c;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;

    u64* dq = (u64*)d;
    while (n >= 8) {
        *dq++ = pattern;
        n -= 8;
    }

    d = (u8*)dq;
    while (n--) {
        *d++ = (u8)c;
    }

    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) {
    u8* d = (u8*)dest;
    const u8* s = (const u8*)src;

    // Align destination if unaligned
    while (n && ((uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }

    u64* dq = (u64*)d;
    const u64* sq = (const u64*)s;

    while (n >= 8) {
        *dq++ = *sq++;
        n -= 8;
    }

    d = (u8*)dq;
    s = (const u8*)sq;

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    u8* d = (u8*)dest;
    const u8* s = (const u8*)src;

    if (d < s || d >= s + n) {
        // No overlap: safe to copy forward
        return memcpy(d, s, n);
    } else {
        // Overlap: copy backwards
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
        return dest;
    }
}

int memcmp(const void* a, const void* b, size_t n) {
    const u8* p1 = (const u8*)a;
    const u8* p2 = (const u8*)b;

    // Handle unaligned prefix
    while (n && (((uintptr_t)p1 & 7) || ((uintptr_t)p2 & 7))) {
        if (*p1 != *p2)
            return (int)*p1 - (int)*p2;
        p1++;
        p2++;
        n--;
    }

    // Compare 8 bytes at a time
    const u64* q1 = (const u64*)p1;
    const u64* q2 = (const u64*)p2;

    while (n >= 8) {
        if (*q1 != *q2) {
            // Find exact differing byte
            p1 = (const u8*)q1;
            p2 = (const u8*)q2;
            for (int i = 0; i < 8; i++) {
                if (p1[i] != p2[i])
                    return (int)p1[i] - (int)p2[i];
            }
        }
        q1++;
        q2++;
        n -= 8;
    }

    // Tail bytes
    p1 = (const u8*)q1;
    p2 = (const u8*)q2;

    while (n--) {
        if (*p1 != *p2)
            return (int)*p1 - (int)*p2;
        p1++;
        p2++;
    }

    return 0;
}

size_t strlen(const char* s) {
    const char* start = s;
    while (*s) s++;
    return s - start;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;

    while (len < maxlen && s[len] != '\0') {
        len++;
    }

    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* strdup(const char* s) {
    size_t l = strlen(s);
    char* d = kmalloc(l + 1);
    if (!d) { return NULL; }
    return memcpy(d, s, l + 1);
}

static int strtol_is_space(char c) {
    return c == ' '  || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int strtol_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }

    return -1;
}

// TODO: errno
long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;

    while (strtol_is_space(*s)) {
        s++;
    }

    int negative = 0;

    if (*s == '+' || *s == '-') {
        negative = (*s == '-');
        s++;
    }

    // invalid bases, return zero and leave endptr at nptr
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && strtol_digit(s[2]) >= 0 && strtol_digit(s[2]) < 16) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && strtol_digit(s[2]) >= 0 && strtol_digit(s[2]) < 16) {
        s += 2;
    }

    const char* digits_start = s;

    // unsigned magnitude permits LONG_MIN to be parsed safely
    unsigned long limit = negative ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;

    unsigned long value = 0;
    int overflow = 0;

    for (;;) {
        int digit = strtol_digit(*s);

        if (digit < 0 || digit >= base) {
            break;
        }

        if (value > (limit - (unsigned long)digit) / (unsigned long)base) {
            overflow = 1;
            value = limit;
        } else if (!overflow) {
            value = value * (unsigned long)base + (unsigned long)digit;
        }

        s++;
    }

    if (s == digits_start) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }

    if (endptr) {
        *endptr = (char*)s;
    }

    if (overflow) {
        return negative ? LONG_MIN : LONG_MAX;
    }

    if (negative) {
        if (value == (unsigned long)LONG_MAX + 1UL) {
            return LONG_MIN;
        }

        return -(long)value;
    }

    return (long)value;
}