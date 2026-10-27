#include <filesystem/vfs/vfs.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <kernel.h>

void vfs_fat32_test() {
    static const char message[] = "Hello from XenonOS!\n";
    char readback[sizeof(message)] = {0};

    printf("[fat32-test] Creating /hellolongfilename.txt\n");

    VFS_FILE* file = vfs_create("/hellolongfilename.txt");

    if (!file) {
        printf("[fat32-test] FAIL: create failed (check that hellolongfilename.txt does not already exist)\n");
        return;
    }

    u32 expected = (u32)(sizeof(message) - 1);
    u32 written = vfs_write(file, message, expected);

    if (written != expected) {
        printf("[fat32-test] FAIL: short write\n");
        vfs_close(file);
        return;
    }

    if (vfs_get_size(file) != expected) {
        printf("[fat32-test] FAIL: in-memory size mismatch\n");
        vfs_close(file);
        return;
    }

    if (vfs_seek(file, 0) != 0) {
        printf("[fat32-test] FAIL: seek failed\n");
        vfs_close(file);
        return;
    }

    u32 bytes_read = vfs_read(file, readback, expected);

    if (bytes_read != expected || memcmp(readback, message, expected) != 0) {
        printf("[fat32-test] FAIL: readback mismatch\n");
        vfs_close(file);
        return;
    }

    vfs_close(file);

    file = vfs_open("/hellolongfilename.txt");

    if (!file) {
        printf("[fat32-test] FAIL: reopen failed\n");
        return;
    }

    memset(readback, 0, sizeof(readback));

    if (vfs_get_size(file) != expected) {
        printf("[fat32-test] FAIL: persisted size mismatch\n");
        vfs_close(file);
        return;
    }

    bytes_read = vfs_read(file, readback, expected);

    if (bytes_read != expected ||
        memcmp(readback, message, expected) != 0) {
        printf("[fat32-test] FAIL: persisted data mismatch\n");
        vfs_close(file);
        return;
    }

    vfs_close(file);

    printf("[fat32-test] PASS: create, write, read, close, reopen\n");
}