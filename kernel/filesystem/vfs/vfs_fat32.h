#pragma once
#include "vfs.h"

extern VFS_FILESYSTEM fat32_ops;

VFS_FILE* fat32_vfs_open(void* fs_data, const char* path);
void fat32_vfs_close(VFS_FILE* file);
u32 fat32_vfs_read(VFS_FILE* file, void* buffer, u32 size);
u32 fat32_vfs_get_size(VFS_FILE* file);
int fat32_vfs_seek(VFS_FILE* file, u32 offset);
u32 fat32_vfs_tell(VFS_FILE* file);