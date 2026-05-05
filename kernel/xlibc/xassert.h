#pragma once

#ifdef XENONOS_DEBUG
    extern void serial_write_char(char c);
    extern void serial_write_str(const char* s);
    extern void serial_write_hex(uint64_t val);
    extern void serial_write_dec(uint64_t val);
    #define xassert(x, msg) if (!(x)) { \
        serial_write_str("Assertion failed! Line: "); \
        serial_write_dec(__LINE__); \
        serial_write_str(" msg: "); \
        serial_write_str(msg); \
        serial_write_char('\n'); \
        while(1); \
    }
#else
    #define xassert(x, msg)
#endif