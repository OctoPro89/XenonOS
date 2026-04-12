#include "vfs_fat32.h"
#include <xlibc/xstddef.h>
#include <xlibc/stdlib.h>
#include <filesystem/fat32/fat32.h>

// stateless ops table
VFS_FILESYSTEM fat32_ops = {
    .fs_data = NULL, // not used here
    .open = fat32_vfs_open,
    .close = fat32_vfs_close,
    .read = fat32_vfs_read,
    .get_size = fat32_vfs_get_size,
    .seek = fat32_vfs_seek,
    .tell = fat32_vfs_tell,
};

VFS_FILE* fat32_vfs_open(void* fs_data, const char* path) {
    FAT32_FS* fs = (FAT32_FS*)fs_data;

    FAT32_FILE* f = fat32_open(fs, path);
    if (!f) { return NULL; }

    VFS_FILE* vf = kmalloc(sizeof(VFS_FILE));
    vf->internal = f;
    vf->fs = &fat32_ops; // set later

    return vf;
}

void fat32_vfs_close(VFS_FILE* file) {
    fat32_close((FAT32_FILE*)file->internal);
    free(file);
}

u32 fat32_vfs_read(VFS_FILE* file, void* buffer, u32 size) {
    FAT32_FILE* f = (FAT32_FILE*)file->internal;
    return fat32_read(f, buffer, size);
}

u32 fat32_vfs_get_size(VFS_FILE* file) {
    FAT32_FILE* f = (FAT32_FILE*)file->internal;
    return fat32_get_size(f);
}

int fat32_vfs_seek(VFS_FILE* file, u32 offset) {
    return fat32_seek((FAT32_FILE*)file->internal, offset);
}

u32 fat32_vfs_tell(VFS_FILE* file) {
    return fat32_tell((FAT32_FILE*)file->internal);
}