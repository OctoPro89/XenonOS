#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <xlibc/stdio.h>
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

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        printf("Usage: de_setvar [options]\n");
        printf("\nOptions:\n");
        printf("  --set-wallpaper <path>   Set the desktop background to the specified image\n");
        printf("  --help                   Display this help message\n");
        return 0;
    }

    if (argc == 3 && argv && strcmp(argv[1], "--set-wallpaper") == 0) {
        if (strlen(argv[2]) < USER_PATH_MAX) {
            syscall_set_desktop_env_var(DESKTOP_ENV_VAR_WALLPAPER_IMAGE_PATH, argv[2], strlen(argv[2]));
            return 0;
        }
        else {
            printf("de_setvar: path longer than USER_PATH_MAX (%u bytes). Please use a shorter path.\n", (u32)USER_PATH_MAX);
            return -1;
        }
    }

    printf("Error: Unknown option '%s'\n\n", argv[1]);
    return -1;
}