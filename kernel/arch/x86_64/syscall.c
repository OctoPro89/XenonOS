#include "syscall.h"
#include <arch/x86_64/io.h>

u64 user_rsp_save;

void syscall_handler() {
    serial_write_str("syscall hit\n");
}