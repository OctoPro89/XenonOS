#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define CAT_BUFFER_SIZE 4096

static int copy_stream(FILE* in, const char* name) {
    char buffer[CAT_BUFFER_SIZE];
    size_t n;
    int failed = 0;

    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (write(STDOUT_FILENO, buffer, n) != n) {
            printf("cat: write error\n");
            return 1;
        }
    }

    /*
    if (ferror(in)) {
        printf("cat: %s: read error\n", name);
        failed = 1;
    }
    */

    return failed;
}

int main(int argc, char** argv) {
    int status = 0;

    /*
    if (argc == 1) {
        return copy_stream(stdin, "-");
    }
    */

    for (int i = 1; i < argc; ++i) {
        const char* path = argv[i];

        /*
        if (strcmp(path, "-") == 0) {
            status |= copy_stream(stdin, "-");
            continue;
        }
    */

        FILE* file = fopen(path, "rb");
        if (!file) {
            printf("cat: %s: cannot open file\n", path);
            status = 1;
            continue;
        }

        status |= copy_stream(file, path);

        if (fclose(file) != 0) {
            printf("cat: %s: close error\n", path);
            status = 1;
        }
    }

    return status;
}