#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <xlibc/string.h>
#include <arch/x86_64/syscall.h>

ssize_t syscall_set_desktop_env_var(desktop_env_var_t var, void* val, size_t length) {
    long ret;

    asm volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_SET_DESKTOP_ENV_VAR),
          "D"(var),
          "S"(val),
          "d"(length)
        : "rcx", "r11", "r10", "memory"
    );

    return (ssize_t)ret;
}

int main(void) {
    char path[] = "haimiya.png";
    syscall_set_desktop_env_var(DESKTOP_ENV_VAR_WALLPAPER_IMAGE_PATH, path, strlen(path));
    return 0;
}