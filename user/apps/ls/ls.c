#include <unistd.h>
#include <xlibc/dirent.h>
#include <xlibc/stdio.h>

int main(void) {
    DIR* dir = opendir("/");
    if (!dir) {
        printf("ls: cannot open directory\n");
        return 1;
    }

    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        printf("%s", entry->d_name);

        if (entry->d_type == DT_DIR) {
            printf("/");
        }

        printf("\n");
    }

    if (closedir(dir) < 0) {
        printf("ls: error closing directory\n");
        return 1;
    }

    return 0;
}