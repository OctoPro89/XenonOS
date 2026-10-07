#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

#include <arch/x86_64/syscall.h>
#include <process.h>
#include <unistd.h>
#include <xlibc/stdio.h>

static u64 syscall1(u64 number, u64 arg1) {
    u64 result;

    asm volatile(
        "syscall"
        : "=a"(result)
        : "a"(number),
          "D"(arg1)
        : "rcx", "r11", "r10", "memory"
    );

    return result;
}

__attribute__((noreturn)) void exit_process(int status) {
    syscall1(SYS_EXIT, (u64)status);

    for (;;) {
        asm volatile("hlt");
    }
}

int main(void) {
    static const char prompt[] = "xenonos@xe:/$ ";
    char line[256];

    for (;;) {
        write(STDOUT_FILENO, prompt, sizeof(prompt) - 1);

        ssize_t size = read(0, line, sizeof(line) - 1);

        if (size <= 0) {
            continue;
        }

        line[size] = 0;

        if (line[size - 1] == '\n') {
            line[size - 1] = 0;
        }

        if (line[0] == 0) {
            continue;
        }

        char path[256];

        snprintf(path, sizeof(path), "bin/%s", line);
        printf("Spawning: %s\n", path);

        pid_t pid = spawn(path);

        if ((i64)pid < 0) {
            printf("spawn failed: %lld\n", (long long)(i64)pid);

            continue;
        }

        int status = wait(pid);

        printf("\nprocess %llu exited with return code %d\n", (unsigned long long)pid, status);
    }

    return 0;
}