#include "stdio.h"
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

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;

    VFS_FILE* vf = (VFS_FILE*)stream->vfs_file;
    long new_pos;

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

    return vfs_seek(vf, (u32)new_pos);
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

typedef void (*printf_emit_fn)(void* ctx, char c);

typedef struct {
    char* buffer;
    size_t maxlen;
    size_t pos;
} snprintf_context_t;

typedef struct {
    printf_emit_fn emit;
    void* emit_ctx;
    int written;
} printf_context_t;


static void printf_console_emit(void* ctx, char c) {
    (void)ctx;
    putc(c);
}

static void printf_buffer_emit(void* ctx, char c) {
    snprintf_context_t* s = (snprintf_context_t*)ctx;

    if (s->pos + 1 < s->maxlen)
    {
        s->buffer[s->pos] = c;
    }

    s->pos++;
}

static void printf_emit_count(void* ctx, char c) {
    printf_context_t* p = (printf_context_t*)ctx;

    p->emit(p->emit_ctx, c);
    p->written++;
}


static void printf_string(printf_emit_fn emit, void* ctx, const char* str) {
    if (!str) {
        str = "(null)";
    }

    while (*str) {
        emit(ctx, *str++);
    }
}

static void printf_unsigned(printf_emit_fn emit, void* ctx, unsigned long long number, int radix) {
    char buffer[32];
    int pos = 0;

    do {
        unsigned long long rem = number % radix;
        number /= radix;

        buffer[pos++] = g_HexChars[rem];
    }
    while (number > 0);

    while (--pos >= 0) {
        emit(ctx, buffer[pos]);
    }
}

static void printf_signed(printf_emit_fn emit, void* ctx, long long number, int radix) {
    if (number < 0) {
        emit(ctx, '-');

        unsigned long long mag = (unsigned long long)(-(number + 1)) + 1;

        printf_unsigned(emit, ctx, mag, radix);
    }
    else {
        printf_unsigned(emit, ctx, (unsigned long long)number, radix);
    }
}

static int vprintf_internal(printf_emit_fn emit_fn, void* emit_ctx, const char* fmt, va_list args) {
    printf_context_t out;
    out.emit = emit_fn;
    out.emit_ctx = emit_ctx;
    out.written = 0;

    int state = PRINTF_STATE_NORMAL;
    int length = PRINTF_LENGTH_DEFAULT;

    int radix = 10;
    b8 sign = false;
    b8 number = false;

    while (*fmt) {
        switch (state) {
            case PRINTF_STATE_NORMAL: {
                switch (*fmt) {
                    case '%':
                        state = PRINTF_STATE_LENGTH;
                        break;

                    default:
                        printf_emit_count(&out, *fmt);
                        break;
                }

                break;
            }

            case PRINTF_STATE_LENGTH: {
                switch (*fmt) {
                    case 'h':
                        length = PRINTF_LENGTH_SHORT;
                        state = PRINTF_STATE_LENGTH_SHORT;
                        break;

                    case 'l':
                        length = PRINTF_LENGTH_LONG;
                        state = PRINTF_STATE_LENGTH_LONG;
                        break;

                    default:
                        goto PRINTF_STATE_SPEC_;
                }

                break;
            }

            case PRINTF_STATE_LENGTH_SHORT: {
                if (*fmt == 'h') {
                    length = PRINTF_LENGTH_SHORT_SHORT;
                    state = PRINTF_STATE_SPEC;
                }
                else {
                    goto PRINTF_STATE_SPEC_;
                }

                break;
            }

            case PRINTF_STATE_LENGTH_LONG: {
                if (*fmt == 'l') {
                    length = PRINTF_LENGTH_LONG_LONG;
                    state = PRINTF_STATE_SPEC;
                }
                else {
                    goto PRINTF_STATE_SPEC_;
                }

                break;
            }

            case PRINTF_STATE_SPEC:
            PRINTF_STATE_SPEC_: {
                switch (*fmt) {
                    case 'c': {
                        printf_emit_count(&out, (char)va_arg(args, int));
                        break;
                    }

                    case 's': {
                        const char* str = va_arg(args, const char*);

                        printf_string(printf_emit_count, &out, str);
                        break;
                    }

                    case '%': {
                        printf_emit_count(&out, '%');
                        break;
                    }

                    case 'd':
                    case 'i': {
                        radix = 10;
                        sign = true;
                        number = true;
                        break;
                    }

                    case 'u': {
                        radix = 10;
                        sign = false;
                        number = true;
                        break;
                    }

                    case 'x':
                    case 'X': {
                        radix = 16;
                        sign = false;
                        number = true;
                        break;
                    }

                    case 'o': {
                        radix = 8;
                        sign = false;
                        number = true;
                        break;
                    }

                    case 'p': {
                        unsigned long long ptr = (unsigned long long)va_arg(args, void*);

                        printf_string(printf_emit_count, &out, "0x");
                        printf_unsigned(printf_emit_count, &out, ptr, 16);
                        break;
                    }

                    default:
                        break;
                }

                if (number) {
                    if (sign) {
                        switch (length) {
                            case PRINTF_LENGTH_SHORT_SHORT:
                            case PRINTF_LENGTH_SHORT:
                            case PRINTF_LENGTH_DEFAULT: {
                                printf_signed(printf_emit_count, &out, va_arg(args, int), radix);
                                break;
                            }

                            case PRINTF_LENGTH_LONG: {
                                printf_signed(printf_emit_count, &out, va_arg(args, long), radix);
                                break;
                            }

                            case PRINTF_LENGTH_LONG_LONG: {
                                printf_signed(printf_emit_count, &out, va_arg(args, long long), radix);
                                break;
                            }
                        }
                    }
                    else
                    {
                        switch (length)
                        {
                            case PRINTF_LENGTH_SHORT_SHORT:
                            case PRINTF_LENGTH_SHORT:
                            case PRINTF_LENGTH_DEFAULT: {
                                printf_unsigned(printf_emit_count, &out, va_arg(args, unsigned int), radix);
                                break;
                            }

                            case PRINTF_LENGTH_LONG: {
                                printf_unsigned(printf_emit_count, &out, va_arg(args, unsigned long), radix);
                                break;
                            }

                            case PRINTF_LENGTH_LONG_LONG: {
                                printf_unsigned(printf_emit_count, &out, va_arg(args, unsigned long long), radix);
                                break;
                            }
                        }
                    }
                }

                state = PRINTF_STATE_NORMAL;
                length = PRINTF_LENGTH_DEFAULT;

                radix = 10;
                sign = false;
                number = false;

                break;
            }
        }

        fmt++;
    }

    return out.written;
}


int vprintf(const char* fmt, va_list args) {
    return vprintf_internal(printf_console_emit, NULL, fmt, args);
}

// TODO: needs support for field width, i.e. %04x, differentiate %x from %X (lowercase & uppercase), maybe zero-padded representations
int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int result = vprintf(fmt, args);

    va_end(args);

    return result;
}

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args) {
    snprintf_context_t ctx;

    ctx.buffer = buffer;
    ctx.maxlen = size;
    ctx.pos = 0;

    int result = vprintf_internal(printf_buffer_emit, &ctx, fmt, args);

    if (size > 0) {
        size_t term;

        if (ctx.pos < (size - 1)) {
            term = ctx.pos;
        }
        else {
            term = size - 1;
        }

        buffer[term] = '\0';
    }

    return result;
}

int snprintf(char* buffer, size_t size, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);

    int result = vsnprintf(buffer, size, fmt, args);

    va_end(args);

    return result;
}