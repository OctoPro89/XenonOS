#include "stdio.h"
#include <filesystem/vfs/vfs.h>
#include <xlibc/string.h>
#include <memory/heap.h>
#include <arch/x86_64/io.h>

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

    VFS_FILE* vf = NULL;

    switch (primary) {
        case 'r': {
            // existing file required
            vf = vfs_open(path);
            break;
        }
        case 'w': {
            // if the file exists, truncate it, otherwise, create it
            vf = vfs_open(path);

            if (vf) {
                if (vfs_truncate(vf, 0) != 0) {
                    vfs_close(vf);
                    return NULL;
                }

                // NOTE: technicall not needed but just to be extra safe
                if (vfs_seek(vf, 0) != 0) {
                    vfs_close(vf);
                    return NULL;
                }
            }
            else {
                vf = vfs_create(path);
            }

            break;
        }
        case 'a': {
            vf = vfs_open(path);

            if (!vf) {
                vf = vfs_create(path);
            }
            break;
        }
        default: {
            return NULL;
        }
    }

    if (!vf) {
        return NULL;
    }

    FILE* f = kmalloc(sizeof(FILE));

    if (!f) {
        vfs_close(vf);
        return NULL;
    }

    memset(f, 0, sizeof(*f));

    f->vfs_file = vf;
    f->readable = (primary == 'r') || plus;
    f->writable = (primary != 'r') || plus;
    f->append = (primary == 'a');

    return f;
}

u32 fread(void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) {
        return 0;
    }

    if (size == 0 || count == 0) {
        return 0;
    }

    if (!stream->vfs_file || !stream->readable || !ptr) {
        stream->error = 1;
        return 0;
    }

    if (count > UINT32_MAX / size) {
        stream->error = 1;
        return 0;
    }

    u32 total = size * count;
    u32 bytes_read = 0;
    VFS_FILE* vf = (VFS_FILE*)stream->vfs_file;

    while (bytes_read < total) {
        u32 n = vfs_read(
            vf,
            (u8*)ptr + bytes_read,
            total - bytes_read
        );

        if (n == 0 || n > total - bytes_read) {
            if (n > total - bytes_read) {
                stream->error = 1;
            } else if (vfs_tell(vf) >= vfs_get_size(vf)) {
                stream->eof = 1;
            } else {
                stream->error = 1;
            }
            break;
        }

        bytes_read += n;
    }

    return bytes_read / size;
}

int fclose(FILE* stream) {
    if (!stream) {
        return -1;
    }

    if (stream->vfs_file) {
        vfs_close((VFS_FILE*)stream->vfs_file);
    }

    kfree(stream);
    return 0;
}

int fgetc(FILE* f) {
    if (!f) { return -1; }
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return -1;
    return c;
}

// TODO: check that this is correct
long ftell(FILE* f) {
    if (!f) { return -1; }
    VFS_FILE* vf = f->vfs_file;
    return (long)vfs_tell(vf);
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

u32 fwrite(const void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) {
        return 0;
    }

    if (!stream->vfs_file || !stream->writable ||
        (!ptr && size != 0 && count != 0)) {
        stream->error = 1;
        return 0;
    }

    if (size == 0 || count == 0) {
        return 0;
    }

    if (count > UINT32_MAX / size) {
        stream->error = 1;
        return 0;
    }

    u32 total = size * count;
    u32 written = 0;
    const u8* src = (const u8*)ptr;
    VFS_FILE* vf = (VFS_FILE*)stream->vfs_file;

    if (stream->append) {
        if (vfs_seek(vf, vfs_get_size(vf)) < 0) {
            stream->error = 1;
            return 0;
        }
    }

    while (written < total) {
        u32 n = vfs_write(vf, (void*)(src + written), total - written);

        if (n == 0 || n > total - written) {
            stream->error = 1;
            break;
        }

        written += n;
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

    if (len > UINT32_MAX ||
        fwrite(str, 1, (u32)len, stream) != (u32)len) {
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
    if (!stream) {
        // All-stream flushing needs a registry of open FILE objects.
        return -1;
    }

    if (!stream->vfs_file) {
        stream->error = 1;
        return -1;
    }

    return 0;
}

// TODO: need a better solution
#include <arch/x86_64/sync/sync.h>

extern u32 system_console_write_len;
extern char system_console_buffer[1024];
extern spinlock_t system_console_lock;
extern b8 system_console_initialized;

extern void stdio_impl_putchr(char c);

void putc(char c)
{
    switch (c)
    {
        case '\t': {
            u64 flags = 0;
            spin_lock_irqsave(&system_console_lock, &flags);
            if (system_console_initialized) {
                if (system_console_write_len + 4 < 1024) {
                    memset((void*)&system_console_buffer[system_console_write_len], ' ', 4);
                    system_console_write_len += 4;
                }
            }
            else {
                for (int i = 0; i < 4; i++) { stdio_impl_putchr(' '); }
            }
            spin_unlock_irqrestore(&system_console_lock, flags);
            break;
        }

        case '\r':
            break;

        default: {
            u64 flags = 0;
            spin_lock_irqsave(&system_console_lock, &flags);
            if (system_console_initialized) {
                if (system_console_write_len + 1 < 1024) {
                    system_console_buffer[system_console_write_len++] = c;
                }
            }
            else {
                stdio_impl_putchr(c);
            }

            spin_unlock_irqrestore(&system_console_lock, flags);
            break;
        }
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

static void printf_repeat(printf_emit_fn emit, void* ctx, char c, int count) {
    while (count-- > 0) {
        emit(ctx, c);
    }
}

static void printf_integer_formatted(
    printf_emit_fn emit,
    void* ctx,
    unsigned long long value,
    b8 negative,
    int radix,
    b8 uppercase,
    int width,
    int precision,
    b8 precision_set,
    b8 left_align,
    b8 zero_pad) {
    char digits[32];
    int digit_count = 0;

    const char* alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    // precision zero with value zero produces no digits
    if (value != 0 || !precision_set || precision != 0) {
        do {
            digits[digit_count++] = alphabet[value % radix];
            value /= radix;
        } while (value != 0);
    }

    int zeros = precision_set && precision > digit_count ? precision - digit_count : 0;

    int sign_chars = negative ? 1 : 0;
    int content_width = sign_chars + zeros + digit_count;
    int padding = width > content_width ? width - content_width : 0;

    // zero-padding from the width flag applies after the sign, but only when integer precision wasn't explicitly specified
    if (zero_pad && !left_align && !precision_set) {
        zeros += padding;
        padding = 0;
    }

    if (!left_align) {
        printf_repeat(emit, ctx, ' ', padding);
    }

    if (negative) {
        emit(ctx, '-');
    }

    printf_repeat(emit, ctx, '0', zeros);

    while (digit_count > 0) {
        emit(ctx, digits[--digit_count]);
    }

    if (left_align) {
        printf_repeat(emit, ctx, ' ', padding);
    }
}

static int vprintf_internal(printf_emit_fn emit_fn, void* emit_ctx, const char* fmt, va_list args) {
    printf_context_t out = {
        .emit = emit_fn,
        .emit_ctx = emit_ctx,
        .written = 0
    };

    while (*fmt) {
        if (*fmt != '%') {
            printf_emit_count(&out, *fmt++);
            continue;
        }

        ++fmt;

        if (*fmt == '%') {
            printf_emit_count(&out, '%');
            ++fmt;
            continue;
        }

        // flags
        b8 left_align = false;
        b8 zero_pad = false;

        for (;;) {
            if (*fmt == '-') {
                left_align = true;
                ++fmt;
            } else if (*fmt == '0') {
                zero_pad = true;
                ++fmt;
            } else {
                break;
            }
        }

        // field width
        int width = 0;

        if (*fmt == '*') {
            width = va_arg(args, int);
            ++fmt;

            if (width < 0) {
                left_align = true;
                width = -width;
            }
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                if (width < 100000) {
                    width = width * 10 + (*fmt - '0');
                }
                ++fmt;
            }
        }

        // integer/string precision
        int precision = 0;
        b8 precision_set = false;

        if (*fmt == '.') {
            precision_set = true;
            ++fmt;

            if (*fmt == '*') {
                precision = va_arg(args, int);
                ++fmt;

                if (precision < 0) {
                    precision_set = false;
                    precision = 0;
                }
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    if (precision < 100000) {
                        precision = precision * 10 + (*fmt - '0');
                    }
                    ++fmt;
                }
            }
        }

        // length modifier
        enum {
            LEN_DEFAULT,
            LEN_HH,
            LEN_H,
            LEN_L,
            LEN_LL
        } length = LEN_DEFAULT;

        if (fmt[0] == 'h' && fmt[1] == 'h') {
            length = LEN_HH;
            fmt += 2;
        } else if (*fmt == 'h') {
            length = LEN_H;
            ++fmt;
        } else if (fmt[0] == 'l' && fmt[1] == 'l') {
            length = LEN_LL;
            fmt += 2;
        } else if (*fmt == 'l') {
            length = LEN_L;
            ++fmt;
        }

        char spec = *fmt;
        if (spec == '\0') {
            break;
        }
        ++fmt;

        switch (spec) {
            case 'c': {
                char c = (char)va_arg(args, int);
                int padding = width > 1 ? width - 1 : 0;

                if (!left_align) {
                    printf_repeat(printf_emit_count, &out, ' ', padding);
                }

                printf_emit_count(&out, c);

                if (left_align) {
                    printf_repeat(printf_emit_count, &out, ' ', padding);
                }
                break;
            }

            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) {
                    s = "(null)";
                }

                int len = 0;
                while (s[len] &&
                       (!precision_set || len < precision)) {
                    ++len;
                }

                int padding = width > len ? width - len : 0;

                if (!left_align) {
                    printf_repeat(printf_emit_count, &out, ' ', padding);
                }

                for (int i = 0; i < len; ++i) {
                    printf_emit_count(&out, s[i]);
                }

                if (left_align) {
                    printf_repeat(printf_emit_count, &out, ' ', padding);
                }
                break;
            }

            case 'd':
            case 'i': {
                long long value;

                switch (length) {
                    case LEN_HH:
                        value = (signed char)va_arg(args, int);
                        break;
                    case LEN_H:
                        value = (short)va_arg(args, int);
                        break;
                    case LEN_L:
                        value = va_arg(args, long);
                        break;
                    case LEN_LL:
                        value = va_arg(args, long long);
                        break;
                    default:
                        value = va_arg(args, int);
                        break;
                }

                b8 negative = value < 0;
                unsigned long long magnitude = negative ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;

                printf_integer_formatted(
                    printf_emit_count, &out, magnitude, negative,
                    10, false, width, precision, precision_set,
                    left_align, zero_pad
                );
                break;
            }

            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'p': {
                unsigned long long value;

                if (spec == 'p') {
                    value = (unsigned long long)(uintptr_t)va_arg(args, void*);
                } else {
                    switch (length) {
                        case LEN_HH:
                            value = (unsigned char)va_arg(args, unsigned int);
                            break;
                        case LEN_H:
                            value = (unsigned short)va_arg(args, unsigned int);
                            break;
                        case LEN_L:
                            value = va_arg(args, unsigned long);
                            break;
                        case LEN_LL:
                            value = va_arg(args, unsigned long long);
                            break;
                        default:
                            value = va_arg(args, unsigned int);
                            break;
                    }
                }

                int radix = 10;
                if (spec == 'x' || spec == 'X' || spec == 'p') {
                    radix = 16;
                } else if (spec == 'o') {
                    radix = 8;
                }

                if (spec == 'p') {
                    printf_string(printf_emit_count, &out, "0x");
                    if (width >= 2) {
                        width -= 2;
                    }
                }

                printf_integer_formatted(
                    printf_emit_count, &out, value, false,
                    radix, spec == 'X', width, precision,
                    precision_set, left_align, zero_pad
                );
                break;
            }

            default:
                // preserve unsupported conversions literally
                printf_emit_count(&out, '%');
                printf_emit_count(&out, spec);
                break;
        }
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