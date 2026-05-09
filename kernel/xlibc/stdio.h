#pragma once
#include <xlibc/xstdint.h>

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
int fseek(FILE* stream, int offset, int whence);

void putc(char c);
void puts(const char* str);
void printf(const char* fmt, ...);