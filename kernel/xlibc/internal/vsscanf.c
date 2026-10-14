#include <xlibc/stdio.h>
#include <xlibc/string.h>

#include <stdarg.h>
#include <stddef.h>

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// TODO: look at this
int vsscanf(const char *buf, const char *fmt, va_list ap) {
    int matched = 0;
    
    while (*fmt) {
        if (is_space(*fmt)) {
            while (is_space(*fmt)) fmt++;
            while (is_space(*buf)) buf++;
            continue;
        }

        if (*fmt != '%') {
            if (*buf != *fmt) break;
            buf++;
            fmt++;
            continue;
        }

        fmt++; // move past '%'

        if (*fmt == '%') {
            if (*buf != '%') break;
            buf++;
            fmt++;
            continue;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (width == 0) width = -1;

        if (*fmt != 'c') {
            while (is_space(*buf)) buf++;
        }

        if (*buf == '\0') {
            if (matched == 0) return -1; // EOF
            break;
        }

        char spec = *fmt++;
        
        switch (spec) {
            case 'c': {
                char *p = va_arg(ap, char *);
                if (width == -1) width = 1; // %c reads exactly 1 char if no width
                
                int i = 0;
                while (i < width && *buf != '\0') {
                    *p++ = *buf++;
                    i++;
                }
                matched++;
                break;
            }

            case 's': {
                char *p = va_arg(ap, char *);
                int i = 0;
                // %s reads non-whitespace characters up to width limit
                while (*buf != '\0' && !is_space(*buf) && (width == -1 || i < width)) {
                    *p++ = *buf++;
                    i++;
                }
                *p = '\0';
                matched++;
                break;
            }

            case 'd':
            case 'u':
            case 'x':
            case 'X': {
                int base = 10;
                int is_signed = (spec == 'd');
                if (spec == 'x' || spec == 'X') base = 16;

                int sign = 1;
                if (is_signed && *buf == '-') {
                    sign = -1;
                    buf++;
                    if (width != -1) width--;
                } else if (is_signed && *buf == '+') {
                    buf++;
                    if (width != -1) width--;
                }

                if (base == 16 && *buf == '0' && (*(buf + 1) == 'x' || *(buf + 1) == 'X')) {
                    buf += 2;
                    if (width != -1) width -= 2;
                }

                unsigned long long val = 0;
                int digit_count = 0;

                while (*buf != '\0' && (width == -1 || digit_count < width)) {
                    int d = hex_val(*buf);
                    if (d < 0 || d >= base) break;
                    val = val * base + d;
                    buf++;
                    digit_count++;
                }

                if (digit_count == 0) {
                    goto parsing_done; 
                }

                if (is_signed) {
                    int *p = va_arg(ap, int *);
                    *p = (int)(val * sign);
                } else {
                    unsigned int *p = va_arg(ap, unsigned int *);
                    *p = (unsigned int)val;
                }
                matched++;
                break;
            }

            default:
                goto parsing_done;
        }
    }

parsing_done:
    return matched;
}