#pragma once
#include "vfs.h"

extern VFS_FILESYSTEM fat32_ops;

VFS_FILE* fat32_vfs_open(void* fs_data, const char* path);
void fat32_vfs_close(VFS_FILE* file);
VFS_FILE* fat32_vfs_create(void* fs_data, const char* path);
u32 fat32_vfs_read(VFS_FILE* file, void* buffer, u32 size);
u32 fat32_vfs_write(VFS_FILE* file, const void* buffer, u32 size);
u32 fat32_vfs_get_size(VFS_FILE* file);
int fat32_vfs_truncate(VFS_FILE* file, u32 size);
int fat32_vfs_seek(VFS_FILE* file, u32 offset);
u32 fat32_vfs_tell(VFS_FILE* file);

VFS_DIR* fat32_vfs_opendir(void* fs_data, const char* path);
int fat32_vfs_readdir(VFS_DIR* dir, VFS_DIRENT* out);
void fat32_vfs_closedir(VFS_DIR* dir);