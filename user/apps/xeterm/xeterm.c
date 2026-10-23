#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>

#include <arch/x86_64/syscall.h>
#include <process.h>
#include <unistd.h>
#include <xlibc/stdio.h>

int main(void) {
    // create window
    window_config_t wconfig = {
        .title = "xeterm",
        .rect = {
            .x = 180,
            .y = 120,
            .width = 480,
            .height = 313
        },
        .type = WINDOW_TYPE_NORMAL,
        .flags = 0
    };

    if (window_create(&wconfig) < 0) {
        return -1;
    }

    window_setup_terminal_io();

    char cwd[256];
    char prompt[256];
    char line[256];
    
    for (;;) {
        write(STDOUT_FILENO, "\033[38;2;35;209;139mxenonos@xe", sizeof("\033[38;2;35;209;139mxenonos@xe") - 1);
        write(STDOUT_FILENO, "\033[38;2;204;204;204m:", sizeof("\033[38;2;204;204;204m:") - 1);
        write(STDOUT_FILENO, "\033[38;2;56;131;216m", sizeof("\033[38;2;56;131;216m") - 1);
        if (getcwd(cwd, sizeof(cwd))) {
            write(STDOUT_FILENO, cwd, strlen(cwd));
        }

        write(STDOUT_FILENO, "\033[38;2;204;204;204m$ ", sizeof("\033[38;2;204;204;204m$ ") - 1);

        ssize_t size = read(STDIN_FILENO, line, sizeof(line) - 1);

        if (size <= 0) {
            continue;
        }

        line[size] = '\0';

        if (line[0] == 0) {
            continue;
        }

        if (size > 0 && line[size - 1] == '\n') {
            line[--size] = '\0';
        }

        if (size > 0 && line[size - 1] == '\r') {
            line[--size] = '\0';
        }

        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "exit") == 0) {
            window_destroy();
            return 0;
        }

        if (strcmp(line, "pwd") == 0) {
            if (getcwd(cwd, sizeof(cwd))) {
                printf("%s\n", cwd);
            } else {
                printf("pwd: getcwd failed\n");
            }
            continue;
        }

        if (strncmp(line, "cd", 2) == 0 && (line[2] == '\0' || line[2] == ' ' || line[2] == '\t')) {
            char* target = line + 2;

            while (*target == ' ' || *target == '\t') {
                ++target;
            }

            if (*target == '\0') {
                target = "/";
            }

            if (chdir(target) < 0) {
                // printf("cd: cannot change directory to %s (errno %d)\n", target, errno);
                printf("cd: cannot change directory to %s\n", target);
            }
            continue;
        }

        if (strncmp(line, "clear", 5) == 0) {
            write(STDOUT_FILENO, "\033[2J", sizeof("\033[2J") - 1);
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "/bin/%s", line);

        pid_t pid = spawn(path);

        if ((i64)pid < 0) {
            // printf("spawn failed (errno %d)\n", errno);
            printf("spawn failed: %lld\n", (long long)(i64)pid);
            continue;
        }

        int status = wait(pid);
        printf("\nprocess %llu exited with return code %d\n", (unsigned long long)pid, status);
    }


    if (!window_destroy()) {
        return -1;
    }

    return 0;
}