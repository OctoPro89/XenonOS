#pragma once
#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

typedef struct vfs_file VFS_FILE;
typedef struct vfs_dir VFS_DIR;

#define VFS_NAME_MAX 255

typedef enum {
    VFS_TYPE_UNKNOWN = 0,
    VFS_TYPE_FILE = 1,
    VFS_TYPE_DIRECTORY = 2,
} VFS_FILE_TYPE;

typedef struct vfs_dirent {
    char name[VFS_NAME_MAX + 1];
    u32 size;
    VFS_FILE_TYPE type;
} VFS_DIRENT;

typedef struct {
    void* fs_data;

    VFS_FILE* (*open)(void* fs_data, const char* path);
    void (*close)(VFS_FILE* file);
    u32 (*read)(VFS_FILE* file, void* buffer, u32 size);
    VFS_FILE* (*create)(void* fs_data, const char* path);
    u32 (*write)(VFS_FILE* file, const void* buffer, u32 size);
    u32 (*get_size)(VFS_FILE* file);

    int (*seek)(VFS_FILE* file, u32 offset);
    u32 (*tell)(VFS_FILE* file);

    // directory operations
    VFS_DIR* (*opendir)(void* fs_data, const char* path);
    int (*readdir)(VFS_DIR* dir, VFS_DIRENT* out);
    void (*closedir)(VFS_DIR* dir);
} VFS_FILESYSTEM;

struct vfs_file {
    void* internal; // FAT32_FILE* etc
    VFS_FILESYSTEM* fs;
};

struct vfs_dir {
    void* internal;
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
VFS_FILE* vfs_create(const char* path);
u32 vfs_write(VFS_FILE* file, const void* buffer, u32 size);
u32 vfs_get_size(VFS_FILE* file);
int vfs_seek(VFS_FILE* file, u32 offset);
u32 vfs_tell(VFS_FILE* file);

VFS_DIR* vfs_opendir(const char* path);

/**
 * @brief Reads a directory
 * @returns 1 for an entry, 0 for end-of-directory, and -1 for an error
 */
int vfs_readdir(VFS_DIR* dir, VFS_DIRENT* out);
void vfs_closedir(VFS_DIR* dir);

int vfs_resolve_path(const char* cwd, const char* path, char* out, size_t out_size);