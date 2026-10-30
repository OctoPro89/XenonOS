#include <filesystem/vfs/vfs.h>
#include <xlibc/string.h>
#include <errno.h>

#define VFS_PATH_MAX 255

static int append_component(char* out, size_t* len, size_t out_size, const char* component, size_t component_len) {
    // "." has no effect
    if (component_len == 1 && component[0] == '.') {
        return 0;
    }

    // ".." removes the last component, but never escapes root
    if (component_len == 2 && component[0] == '.' && component[1] == '.') {
        if ((*len) > 1) {
            while ((*len) > 1 && out[(*len) - 1] != '/') {
                --(*len);
            }

            if ((*len) > 1) {
                --(*len);
            }

            out[(*len)] = '\0';
        }

        return 0;
    }

    // avoid empty components from repeated slashes
    if (component_len == 0) {
        return 0;
    }

    size_t separator = ((*len) > 1) ? 1 : 0;

    if ((*len) >= out_size || separator > out_size - 1 - (*len) || component_len > out_size - 1 - (*len) - separator) {
        return -ENAMETOOLONG;
    }

    if (separator) {
        out[(*len)++] = '/';
    }

    memcpy(out + (*len), component, component_len);
    (*len) += component_len;
    out[(*len)] = '\0';

    return 0;
}

static int normalize_into(char* out, size_t* len, size_t out_size, const char* input) {
    const char* p = input;

    while (*p) {
        // collapse repeated slashes
        while (*p == '/') {
            ++p;
        }

        if ((*p) == '\0') {
            break;
        }

        const char* start = p;

        while ((*p) && (*p) != '/') {
            ++p;
        }

        size_t component_len = (size_t)(p - start);
        int result = append_component(out, len, out_size, start, component_len);

        if (result < 0) { return result; }
    }

    return 0;
}

static VFS_MOUNT root_mount;

void vfs_mount_root(VFS_FILESYSTEM* ops, void* fs_data) {
    root_mount.ops = ops;
    root_mount.fs_data = fs_data;
}

VFS_FILE* vfs_open(const char* path) {
    if (!path || !root_mount.ops) { return NULL; }

    return root_mount.ops->open(root_mount.fs_data, path);
}

void vfs_close(VFS_FILE* file) {
    if (!file || !file->fs || !file->fs->close) { return; }
    file->fs->close(file);
}

u32 vfs_read(VFS_FILE* file, void* buffer, u32 size) {
    if (!file || !file->fs || !file->fs->read || !root_mount.ops || !buffer || !size) { return 0; }

    return file->fs->read(file, buffer, size);
}

VFS_FILE* vfs_create(const char* path) {
    if (!path || !root_mount.ops || !root_mount.ops->create) {
        return NULL;
    }

    return root_mount.ops->create(root_mount.fs_data, path);
}

u32 vfs_write(VFS_FILE* file, const void* buffer, u32 size) {
    if (!file || !file->fs || !file->fs->write || !buffer || size == 0) {
        return 0;
    }

    return file->fs->write(file, buffer, size);
}

int vfs_truncate(VFS_FILE* file, u32 size) {
    if (!file || !file->fs || !file->fs->truncate) {
        return -1;
    }

    return file->fs->truncate(file, size);
}

u32 vfs_get_size(VFS_FILE* file) {
    if (!file || !file->fs || !file->fs->get_size || !root_mount.ops) { return 0; }

    return file->fs->get_size(file);
}

int vfs_seek(VFS_FILE* file, u32 offset) {
    if (!file || !file->fs || !file->fs->seek) { return -1; }
    return file->fs->seek(file, offset);
}

u32 vfs_tell(VFS_FILE* file) {
    if (!file || !file->fs || !file->fs->tell) { return 0; }
    return file->fs->tell(file);
}

VFS_DIR* vfs_opendir(const char* path) {
    if (!path || !root_mount.ops || !root_mount.ops->opendir) { return NULL; }

    VFS_DIR* dir = root_mount.ops->opendir(root_mount.fs_data, path);

    if (!dir) { return NULL; }

    return dir;
}

int vfs_readdir(VFS_DIR* dir, VFS_DIRENT* out) {
    if (!dir || !out || !dir->fs || !dir->fs->readdir) { return -1; }

    return dir->fs->readdir(dir, out);
}

void vfs_closedir(VFS_DIR* dir) {
    if (!dir || !dir->fs || !dir->fs->closedir) { return; }

    dir->fs->closedir(dir);
}

int vfs_resolve_path(const char* cwd, const char* path, char* out, size_t out_size) {
    if (!cwd || !path || !out || out_size < 2) {
        return -EINVAL;
    }

    out[0] = '\0';

    size_t cwd_len = strnlen(cwd, VFS_PATH_MAX + 1);
    size_t path_len = strnlen(path, VFS_PATH_MAX + 1);

    if (cwd_len > VFS_PATH_MAX || path_len > VFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    // cwd must be absolute
    if (cwd[0] != '/') {
        return -EINVAL;
    }

    // start at filesystem root
    out[0] = '/';
    out[1] = '\0';

    size_t len = 1;
    int result;

    // relative paths begin at cwd, absolute paths begin at root
    if (path[0] != '/') {
        result = normalize_into(out, &len, out_size, cwd);
        if (result < 0) {
            out[0] = '\0';
            return result;
        }
    }

    result = normalize_into(out, &len, out_size, path);
    if (result < 0) {
        out[0] = '\0';
        return result;
    }

    return 0;
}