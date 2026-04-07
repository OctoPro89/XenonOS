/*
    libc implementations for string & memory ops
    NOTE: **as of apr 5 2026** all functions are overlap / alignment safe
*/

#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>

void* memset(void* dest, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);

size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);