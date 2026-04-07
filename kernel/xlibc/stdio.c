#include "stdio.h"
#include <filesystem/vfs/vfs.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <arch/x86_64/io.h>

FILE* fopen(const char* path, const char* mode) {
    if (!(strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0)) {
        serial_write_str("fopen modes besides read (r / rb) are not currently implemented!\n");
        return NULL;
    }

    VFS_FILE* vf = vfs_open(path);
    if (!vf) { return NULL; }

    FILE* f = malloc(sizeof(FILE));
    f->vfs_file = vf;

    return f;
}

u32 fread(void* ptr, u32 size, u32 count, FILE* stream) {
    if (!stream) { return 0; }
    if (size == 0 || count == 0) { return 0; } // divide by zero

    u32 total = size * count;

    u32 bytes = vfs_read((VFS_FILE*)stream->vfs_file, ptr, total);

    return bytes / size; // NOTE: return element count, libc semantics
}

int fclose(FILE* stream) {
    if (!stream) return -1;

    vfs_close((VFS_FILE*)stream->vfs_file);
    free(stream);

    return 0;
}

int fgetc(FILE* f) {
    if (!f) { return -1; }
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return -1;
    return c;
}

u32 ftell(FILE* f) {
    if (!f) { return 0; }
    VFS_FILE* vf = f->vfs_file;
    return vfs_tell(vf);
}

int fseek(FILE* stream, int offset, int whence) {
    if (!stream) return -1;

    VFS_FILE* vf = (VFS_FILE*)stream->vfs_file;
    int new_pos;

    switch (whence) {
        case SEEK_SET: {
            new_pos = offset;
            break;
        }
        case SEEK_CUR: {
            new_pos = (int)vfs_tell(vf) + offset;
            break;
        }
        case SEEK_END: {
            new_pos = (int)vfs_get_size(vf) + offset;
            break;
        }
        default: {
            return -1;
        }
    }

    if (new_pos < 0) new_pos = 0;

    return vfs_seek(vf, new_pos);
}