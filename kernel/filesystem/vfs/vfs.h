#pragma once
#include <xlibc/xstdint.h>

typedef struct vfs_file VFS_FILE;

typedef struct {
    void* fs_data;

    VFS_FILE* (*open)(void* fs_data, const char* path);
    void (*close)(VFS_FILE* file);
    u32 (*read)(VFS_FILE* file, void* buffer, u32 size);
    u32 (*get_size)(VFS_FILE* file);

    int (*seek)(VFS_FILE* file, u32 offset);
    u32 (*tell)(VFS_FILE* file);
} VFS_FILESYSTEM;

struct vfs_file {
    void* internal; // FAT32_FILE* etc
    VFS_FILESYSTEM* fs;
};

typedef struct {
    void* fs_data;              // ex. FAT32_FS*
    VFS_FILESYSTEM* ops;        // ex. fat32_ops
} VFS_MOUNT;

void vfs_mount_root(VFS_FILESYSTEM* ops, void* fs_data);
VFS_FILE* vfs_open(const char* path);
void vfs_close(VFS_FILE* file);
u32 vfs_read(VFS_FILE* file, void* buffer, u32 size);
u32 vfs_get_size(VFS_FILE* file);
int vfs_seek(VFS_FILE* file, u32 offset);
u32 vfs_tell(VFS_FILE* file);