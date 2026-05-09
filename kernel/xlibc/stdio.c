#include "stdio.h"
#include <stdarg.h>
#include <filesystem/vfs/vfs.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <arch/x86_64/io.h>

FILE* fopen(const char* path, const char* mode) {
    if (!(strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0)) {
        serial_write_str("fopen modes besides read (r / rb) are not currently implemented!\n");
        return NULL;
    }

    VFS_FILE* vf = vfs_open(path);
    if (!vf) { return NULL; }

    FILE* f = malloc(sizeof(FILE));
    f->vfs_file = vf;

    return f;
}

u32 fread(void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) { return 0; }
    if (size == 0 || count == 0) { return 0; } // divide by zero

    u32 total = size * count;

    u32 bytes = vfs_read((VFS_FILE*)stream->vfs_file, ptr, total);

    return bytes / size; // NOTE: return element count, libc
}

int fclose(FILE* stream) {
    if (!stream) return -1;

    vfs_close((VFS_FILE*)stream->vfs_file);
    free(stream);

    return 0;
}

int fgetc(FILE* f) {
    if (!f) { return -1; }
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return -1;
    return c;
}

u32 ftell(FILE* f) {
    if (!f) { return 0; }
    VFS_FILE* vf = f->vfs_file;
    return vfs_tell(vf);
}

int fseek(FILE* stream, int offset, int whence) {
    if (!stream) return -1;

    VFS_FILE* vf = (VFS_FILE*)stream->vfs_file;
    int new_pos;

    switch (whence) {
        case SEEK_SET: {
            new_pos = offset;
            break;
        }
        case SEEK_CUR: {
            new_pos = (int)vfs_tell(vf) + offset;
            break;
        }
        case SEEK_END: {
            new_pos = (int)vfs_get_size(vf) + offset;
            break;
        }
        default: {
            return -1;
        }
    }

    if (new_pos < 0) new_pos = 0;

    return vfs_seek(vf, new_pos);
}

extern void stdio_impl_putchr(char c);

void putc(char c)
{
    switch (c)
    {
        case '\t':
            for (int i = 0; i < 4; i++) { stdio_impl_putchr(' '); }
            break;

        case '\r':
            break;

        default:
            stdio_impl_putchr(c);
            break;
    }
}

void puts(const char* str)
{
    while(*str)
    {
        putc(*str);
        str++;
    }
}

const char g_HexChars[] = "0123456789abcdef";

void printf_unsigned(unsigned long long number, int radix)
{
    char buffer[32];
    int pos = 0;

    // convert number to ASCII
    do 
    {
        unsigned long long rem = number % radix;
        number /= radix;
        buffer[pos++] = g_HexChars[rem];
    } while (number > 0);

    // print number in reverse order
    while (--pos >= 0)
        putc(buffer[pos]);
}

void printf_signed(long long number, int radix)
{
    if (number < 0)
    {
        putc('-');
        printf_unsigned(-number, radix);
    }
    else printf_unsigned(number, radix);
}

#define PRINTF_STATE_NORMAL         0
#define PRINTF_STATE_LENGTH         1
#define PRINTF_STATE_LENGTH_SHORT   2
#define PRINTF_STATE_LENGTH_LONG    3
#define PRINTF_STATE_SPEC           4

#define PRINTF_LENGTH_DEFAULT       0
#define PRINTF_LENGTH_SHORT_SHORT   1
#define PRINTF_LENGTH_SHORT         2
#define PRINTF_LENGTH_LONG          3
#define PRINTF_LENGTH_LONG_LONG     4

void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int state = PRINTF_STATE_NORMAL;
    int length = PRINTF_LENGTH_DEFAULT;
    int radix = 10;
    b8 sign = false;
    b8 number = false;

    while (*fmt)
    {
        switch (state)
        {
            case PRINTF_STATE_NORMAL:
                switch (*fmt)
                {
                    case '%':   state = PRINTF_STATE_LENGTH;
                                break;
                    default:    putc(*fmt);
                                break;
                }
                break;

            case PRINTF_STATE_LENGTH:
                switch (*fmt)
                {
                    case 'h':   length = PRINTF_LENGTH_SHORT;
                                state = PRINTF_STATE_LENGTH_SHORT;
                                break;
                    case 'l':   length = PRINTF_LENGTH_LONG;
                                state = PRINTF_STATE_LENGTH_LONG;
                                break;
                    default:    goto PRINTF_STATE_SPEC_;
                }
                break;

            case PRINTF_STATE_LENGTH_SHORT:
                if (*fmt == 'h')
                {
                    length = PRINTF_LENGTH_SHORT_SHORT;
                    state = PRINTF_STATE_SPEC;
                }
                else goto PRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_LENGTH_LONG:
                if (*fmt == 'l')
                {
                    length = PRINTF_LENGTH_LONG_LONG;
                    state = PRINTF_STATE_SPEC;
                }
                else goto PRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_SPEC:
            PRINTF_STATE_SPEC_:
                switch (*fmt)
                {
                    case 'c':   putc((char)va_arg(args, int));
                                break;

                    case 's':   
                                puts(va_arg(args, const char*));
                                break;

                    case '%':   putc('%');
                                break;

                    case 'd':
                    case 'i':   radix = 10; sign = true; number = true;
                                break;

                    case 'u':   radix = 10; sign = false; number = true;
                                break;

                    case 'X':
                    case 'x':
                        radix = 16;
                        sign = false;
                        number = true;
                        break;

                    case 'p': {
                        u64 ptr = (u64)va_arg(args, u64);
                        puts("0x");
                        printf_unsigned(ptr, 16);
                        break;
                    }

                    case 'o':   radix = 8; sign = false; number = true;
                                break;

                    // ignore invalid spec
                    default:    break;
                }

                if (number)
                {
                    if (sign)
                    {
                        switch (length)
                        {
                        case PRINTF_LENGTH_SHORT_SHORT:
                        case PRINTF_LENGTH_SHORT:
                        case PRINTF_LENGTH_DEFAULT:     printf_signed(va_arg(args, int), radix);
                                                        break;

                        case PRINTF_LENGTH_LONG:        printf_signed(va_arg(args, long), radix);
                                                        break;

                        case PRINTF_LENGTH_LONG_LONG:   printf_signed(va_arg(args, long long), radix);
                                                        break;
                        }
                    }
                    else
                    {
                        switch (length)
                        {
                        case PRINTF_LENGTH_SHORT_SHORT:
                        case PRINTF_LENGTH_SHORT:
                        case PRINTF_LENGTH_DEFAULT:     printf_unsigned(va_arg(args, unsigned int), radix);
                                                        break;
                                                        
                        case PRINTF_LENGTH_LONG:        printf_unsigned(va_arg(args, unsigned  long), radix);
                                                        break;

                        case PRINTF_LENGTH_LONG_LONG:   printf_unsigned(va_arg(args, unsigned  long long), radix);
                                                        break;
                        }
                    }
                }

                // reset state
                state = PRINTF_STATE_NORMAL;
                length = PRINTF_LENGTH_DEFAULT;
                radix = 10;
                sign = false;
                number = false;
                break;
        }

        fmt++;
    }

    va_end(args);
}