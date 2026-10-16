#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstdarg.h>
#include <xlibc/xstddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE {
    // TODO: remove
#ifdef XENON_KERNEL
    void* vfs_file;   // VFS_FILE*
#else
    int fd;
    int error;
    int eof;
#endif
} FILE;

FILE* fopen(const char* path, const char* mode);
u32 fread(void* ptr, u32 size, u32 count, FILE* stream);
int fclose(FILE* stream);
int fgetc(FILE* f);
long ftell(FILE* f); // NOTE: maybe look at return type
int fseek(FILE* stream, long offset, int whence);

void putc(char c);
void puts(const char* str);

void putchar(int c);

int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list args);

int snprintf(char* buffer, size_t size, const char* fmt, ...);
int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);

int sscanf(const char* restrict s, const char* restrict fmt, ...);
int vsscanf(const char* restrict str, const char* restrict format, va_list ap);

char* strstr(const char* haystack, const char* needle);