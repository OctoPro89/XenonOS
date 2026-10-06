#include <memory/user.h>

#include <memory/vmm.h>
#include <process.h>

#include <xlibc/string.h>
#include <errno.h>

int copy_to_user(void* user_dst, void* kernel_src, size_t size) {
    process_t* process = process_current();
    if (!process) {
        return -EFAULT;
    }

    if (!vmm_user_range_valid(process->space, (vaddr_t)user_dst, size, true)) {
        return -EFAULT;
    }

    memcpy(user_dst, kernel_src, size);
    return 0;
}

int copy_from_user(void* kernel_dst, const void* user_src, size_t size) {
    process_t* process = process_current();
    if (!process) {
        return -EFAULT;
    }

    if (!vmm_user_range_valid(process->space, (vaddr_t)user_src, size, false)) {
        return -EFAULT;
    }

    memcpy(kernel_dst, user_src, size);
    return 0;
}