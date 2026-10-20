#include "vfs_fat32.h"
#include <xlibc/xstddef.h>
#include <memory/heap.h>
#include <filesystem/fat32/fat32.h>
#include <xlibc/string.h>

// stateless ops table
VFS_FILESYSTEM fat32_ops = {
    .fs_data = NULL, // not used here
    .open = fat32_vfs_open,
    .close = fat32_vfs_close,
    .read = fat32_vfs_read,
    .get_size = fat32_vfs_get_size,
    .seek = fat32_vfs_seek,
    .tell = fat32_vfs_tell,
    .opendir  = fat32_vfs_opendir,
    .readdir  = fat32_vfs_readdir,
    .closedir = fat32_vfs_closedir,
};

VFS_FILE* fat32_vfs_open(void* fs_data, const char* path) {
    FAT32_FS* fs = (FAT32_FS*)fs_data;

    if (!fs || !path) { return NULL; }

    FAT32_FILE* f = fat32_open(fs, path);
    if (!f) { return NULL; }

    VFS_FILE* vf = kmalloc(sizeof(VFS_FILE));
    if (!vf) {
        fat32_close(f);
        return NULL;
    }

    vf->internal = f;
    vf->fs = &fat32_ops;

    return vf;
}

void fat32_vfs_close(VFS_FILE* file) {
    if (!file) { return; }
    fat32_close((FAT32_FILE*)file->internal);
    kfree(file);
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

VFS_DIR* fat32_vfs_opendir(void* fs_data, const char* path) {
    FAT32_FS* fs = (FAT32_FS*)fs_data;

    if (!fs || !path) { return NULL; }

    FAT32_DIR* fat_dir = fat32_opendir(fs, path);
    if (!fat_dir) { return NULL; }

    VFS_DIR* vfs_dir = kmalloc(sizeof(VFS_DIR));
    if (!vfs_dir) {
        fat32_closedir(fat_dir);
        return NULL;
    }

    vfs_dir->internal = fat_dir;
    vfs_dir->fs = &fat32_ops;

    return vfs_dir;
}

int fat32_vfs_readdir(VFS_DIR* dir, VFS_DIRENT* out) {
    if (!dir || !dir->internal || !out) { return -1; }

    return fat32_readdir((FAT32_DIR*)dir->internal, out);
}

void fat32_vfs_closedir(VFS_DIR* dir) {
    if (!dir) {
        return;
    }

    fat32_closedir((FAT32_DIR*)dir->internal);
    kfree(dir);
}