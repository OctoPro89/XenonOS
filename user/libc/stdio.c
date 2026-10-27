#include "stdio.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <io/fd.h>

static int parse_stdio_mode(const char* mode, char* primary, int* plus) {
    if (!mode || !mode[0] || !primary || !plus) {
        return 0;
    }

    if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') {
        return 0;
    }

    int seen_plus = 0;
    int seen_binary = 0;

    for (u32 i = 1; mode[i]; ++i) {
        if (mode[i] == '+') {
            if (seen_plus) {
                return 0;
            }
            seen_plus = 1;
        } else if (mode[i] == 'b') {
            if (seen_binary) {
                return 0;
            }
            seen_binary = 1;
        } else {
            return 0;
        }
    }

    *primary = mode[0];
    *plus = seen_plus;
    return 1;
}

FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) {
        return NULL;
    }

    char primary;
    int plus;

    if (!parse_stdio_mode(mode, &primary, &plus)) {
        return NULL;
    }

    int flags;

    switch (primary) {
        case 'r': {
            flags = plus ? O_RDWR : O_RDONLY;
            break;
        }
        case 'w': {
            flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            break;
        }
        case 'a': {
            flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            break;
        }
        default: {
            return NULL;
        }
    }

    ssize_t fd = open(path, flags);

    if (fd < 0) {
        return NULL;
    }

    FILE* f = malloc(sizeof(FILE));

    if (!f) {
        close((fd_t)fd);
        return NULL;
    }

    memset(f, 0, sizeof(*f));

    f->fd = (int)fd;
    f->readable = (primary == 'r') || plus;
    f->writable = (primary != 'r') || plus;
    f->append = (primary == 'a');

    return f;
}

u32 fread(void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) {
        return 0;
    }

    // A zero-sized request performs no I/O.
    if (size == 0 || count == 0) {
        return 0;
    }

    if (!ptr || stream->fd < 0 || !stream->readable) {
        stream->error = 1;
        return 0;
    }

    // Prevent size * count from overflowing.
    if (count > UINT32_MAX / size) {
        stream->error = 1;
        return 0;
    }

    u32 total = size * count;
    u32 bytes_read = 0;
    u8* dst = (u8*)ptr;

    while (bytes_read < total) {
        u32 remaining = total - bytes_read;

        ssize_t result = read((fd_t)stream->fd, dst + bytes_read, remaining);

        if (result < 0) {
            stream->error = 1;
            break;
        }

        if (result == 0) {
            stream->eof = 1;
            break;
        }

        if ((u32)result > remaining) {
            stream->error = 1;
            break;
        }

        bytes_read += (u32)result;
    }

    return bytes_read / size;
}

int fclose(FILE* stream) {
    if (!stream) {
        return -1;
    }

    int result = close((fd_t)stream->fd);

    free(stream);

    return result < 0 ? -1 : 0;
}

long ftell(FILE* stream) {
    if (!stream) {
        return -1;
    }

    ssize_t result = lseek((fd_t)stream->fd, 0, SEEK_CUR);

    if (result < 0) {
        stream->error = 1;
        return -1;
    }

    return (long)result;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) {
        return -1;
    }

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return -1;
    }

    ssize_t result = lseek((fd_t)stream->fd, (ssize_t)offset, whence);

    if (result < 0) {
        stream->error = 1;
        return -1;
    }

    stream->eof = 0;

    return 0;
}

u32 fwrite(const void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) {
        return 0;
    }

    if (size == 0 || count == 0) {
        return 0;
    }

    if (!ptr || stream->fd < 0 || !stream->writable) {
        stream->error = 1;
        return 0;
    }

    if (count > UINT32_MAX / size) {
        stream->error = 1;
        return 0;
    }

    u32 total = size * count;
    u32 written = 0;
    const u8* src = (const u8*)ptr;

    while (written < total) {
        // TODO: O_APPEND should also be enforced by the kernel descriptor; seeking here provides basic append behavior with the current userspace API, but isn't atomic across concurrent writers
        if (stream->append) {
            ssize_t pos = lseek((fd_t)stream->fd, 0, SEEK_END);

            if (pos < 0) {
                stream->error = 1;
                break;
            }
        }

        ssize_t n = write((fd_t)stream->fd, src + written, total - written);

        if (n < 0) {
            stream->error = 1;
            break;
        }

        if (n == 0) {
            stream->error = 1;
            break;
        }

        if ((u32)n > total - written) {
            stream->error = 1;
            break;
        }

        written += (u32)n;
    }

    return written / size;
}

int fputc(int c, FILE* stream) {
    unsigned char ch = (unsigned char)c;

    if (fwrite(&ch, 1, 1, stream) != 1) {
        return -1;
    }

    return (int)ch;
}

int fputs(const char* str, FILE* stream) {
    if (!str || !stream) {
        return -1;
    }

    size_t len = strlen(str);

    if (len > UINT32_MAX || fwrite(str, 1, (u32)len, stream) != (u32)len) {
        return -1;
    }

    return 0;
}

int ferror(FILE* stream) {
    return stream ? stream->error : 1;
}

int feof(FILE* stream) {
    return stream ? stream->eof : 0;
}

void clearerr(FILE* stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int fflush(FILE* stream) {
    if (!stream || stream->fd < 0) {
        return -1;
    }

    return 0;
}

void putc(char c)
{
    switch (c)
    {
        case '\t':
            const char space = ' ';
            for (int i = 0; i < 4; i++) { write(STDOUT_FILENO, (void*)&space, 1); }
            break;

        case '\r':
            break;

        default:
            write(STDOUT_FILENO, (void*)&c, 1);
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

void putchar(int c)
{
    putc(c);
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