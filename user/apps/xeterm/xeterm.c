#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <xlibc/string.h>

#include <arch/x86_64/syscall.h>
#include <process.h>
#include <unistd.h>
#include <xlibc/stdio.h>

#define SHELL_MAX_ARGS 32

static int shell_tokenize(char* line, char** argv, int max_args) {
    char* readp = line;
    char* writep = line;
    int argc = 0;

    while (*readp) {
        while (*readp == ' ' || *readp == '\t') {
            ++readp;
        }

        if (*readp == '\0') {
            break;
        }

        if (argc >= max_args) {
            return -1;
        }

        argv[argc++] = writep;

        char quote = '\0';

        while (*readp) {
            char c = *readp++;

            if (quote) {
                if (c == quote) {
                    quote = '\0';
                } else {
                    *writep++ = c;
                }
                continue;
            }

            if (c == '\'' || c == '"') {
                quote = c;
                continue;
            }

            if (c == '\\') {
                if (*readp == '\0') {
                    return -1;
                }
                *writep++ = *readp++;
                continue;
            }

            if (c == ' ' || c == '\t') {
                break;
            }

            *writep++ = c;
        }

        if (quote) {
            return -1;
        }

        *writep++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

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

        char *argv[SHELL_MAX_ARGS + 1];

        int argc = shell_tokenize(line, argv, SHELL_MAX_ARGS);
        if (argc < 0) {
            printf("xeterm: invalid command line (too many arguments or unmatched quote)\n");
            continue;
        }

        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            window_destroy();
            return 0;
        }

        if (strcmp(argv[0], "pwd") == 0) {
            if (getcwd(cwd, sizeof(cwd))) {
                printf("%s\n", cwd);
            } else {
                printf("pwd: getcwd failed\n");
            }
            continue;
        }

        if (strcmp(argv[0], "cd") == 0) {
            const char* target = (argc >= 2) ? argv[1] : "/";

            if (chdir(target) < 0) {
                printf("cd: cannot change directory to %s\n", target);
            }
            continue;
        }

        if (strcmp(argv[0], "clear") == 0) {
            write(STDOUT_FILENO, "\033[2J", sizeof("\033[2J") - 1);
            continue;
        }

        int has_slash = 0;
        for (const char* p = argv[0]; *p; ++p) {
            if (*p == '/') {
                has_slash = 1;
                break;
            }
        }

        char path[256];

        if (has_slash) {
            int n = snprintf(path, sizeof(path), "%s", argv[0]);
            if (n < 0 || (size_t)n >= sizeof(path)) {
                printf("xeterm: command path too long\n");
                continue;
            }
        } else {
            int n = snprintf(path, sizeof(path), "/bin/%s", argv[0]);
            if (n < 0 || (size_t)n >= sizeof(path)) {
                printf("xeterm: command path too long\n");
                continue;
            }
        }

        pid_t pid = spawn(path, argc, (const char* const*)argv);

        if ((i64)pid < 0) {
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