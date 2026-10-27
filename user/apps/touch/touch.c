#include <xlibc/stdio.h>
#include <xlibc/string.h>
#include <unistd.h>
#include <fcntl.h>
#include <io/fd.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: touch FILE...\n");
        return 0;
    }

    int failed = 0;

    for (int i = 1; i < argc; ++i) {
        const char* path = argv[i];

        if (!path || path[0] == '\0') {
            printf("touch: invalid path\n");
            failed = 1;
            continue;
        }

        ssize_t fd = open(path, O_WRONLY | O_CREAT);

        if (fd < 0) {
            printf("touch: cannot create %s (syscall returned %d)\n", path, (int)fd);
            failed = 1;
            continue;
        }

        if (close((fd_t)fd) < 0) {
            printf("touch: close failed for %s (syscall returned %d)\n", path, (int)fd);
            failed = 1;
        }
    }

    return failed ? 1 : 0;
}