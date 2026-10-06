#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstdarg.h>
#include <xlibc/xstddef.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE {
    void* vfs_file;   // VFS_FILE*
} FILE;

FILE* fopen(const char* path, const char* mode);
u32 fread(void* ptr, u32 size, u32 count, FILE* stream);
int fclose(FILE* stream);
int fgetc(FILE* f);
u32 ftell(FILE* f);
int fseek(FILE* stream, long offset, int whence);

void putc(char c);
void puts(const char* str);

int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list args);

int snprintf(char* buffer, size_t size, const char* fmt, ...);
int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);