#include "vfs.h"
#include <xlibc/xstddef.h>

static VFS_MOUNT root_mount;

void vfs_mount_root(VFS_FILESYSTEM* ops, void* fs_data) {
    root_mount.ops = ops;
    root_mount.fs_data = fs_data;
}

VFS_FILE* vfs_open(const char* path) {
    if (!root_mount.ops) { return NULL; }

    return root_mount.ops->open(root_mount.fs_data, path);
}

void vfs_close(VFS_FILE* file) {
    if (!file || !file->fs) { return; }
    file->fs->close(file);
}

u32 vfs_read(VFS_FILE* file, void* buffer, u32 size) {
    if (!root_mount.ops) { return 0; }

    return file->fs->read(file, buffer, size);
}

u32 vfs_get_size(VFS_FILE* file) {
    if (!root_mount.ops) { return 0; }

    return file->fs->get_size(file);
}

int vfs_seek(VFS_FILE* file, u32 offset) {
    if (!file || !file->fs || !file->fs->seek) return -1;
    return file->fs->seek(file, offset);
}

u32 vfs_tell(VFS_FILE* file) {
    if (!file || !file->fs || !file->fs->tell) return 0;
    return file->fs->tell(file);
}