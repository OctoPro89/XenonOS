#include "fat32.h"
#include <arch/x86_64/io.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <memory/heap.h>
#include <kernel.h>


#include <time/time.h>
static u64 debug_data_cluster_reads = 0;
static u64 debug_fat_cache_hits = 0;
static u64 debug_fat_sector_reads = 0;
static u64 debug_cluster_read_ns = 0;

static b8 fat32_read_fat_entry_checked(FAT32_FS* fs, u32 cluster, u32* out_cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = fs->fat_start_lba + (fat_offset / 512);
    u32 ent_offset = fat_offset % 512;

    if (fs->fat_cache_valid &&
        fs->fat_cache_lba == fat_sector) {
        debug_fat_cache_hits++;
    }

    if (!fs->fat_cache_valid || fs->fat_cache_lba != fat_sector) {
        if (!fs->dev->read(fs->dev->driver_data, fat_sector, 1, fs->fat_cache)) {
            fs->fat_cache_valid = false;
            return false;
        }

        ++debug_fat_sector_reads;
        fs->fat_cache_lba = fat_sector;
        fs->fat_cache_valid = true;
    }

    u32 value;
    memcpy(&value, &fs->fat_cache[ent_offset], sizeof(value));

    *out_cluster = value & 0x0FFFFFFF;
    return true;
}

static b8 fat32_get_cluster_at(FAT32_FILE *f, u32 target_index, u32 *out_cluster) {
    if (!f->cluster_cursor_valid || target_index < f->current_cluster_index) {
        f->current_cluster = f->first_cluster;
        f->current_cluster_index = 0;
        f->cluster_cursor_valid = true;
    }

    while (f->current_cluster_index < target_index) {
        u32 next_cluster;

        if (!fat32_read_fat_entry_checked(f->fs, f->current_cluster, &next_cluster)) {
            f->cluster_cursor_valid = false;
            return false;
        }

        if (next_cluster < 2 || next_cluster >= 0x0FFFFFF0) {
            f->cluster_cursor_valid = false;
            return false;
        }

        f->current_cluster = next_cluster;
        f->current_cluster_index++;
    }

    *out_cluster = f->current_cluster;
    return true;
}

static int fat32_is_data_cluster(u32 cluster) {
    // exclude free, reserved, bad, and end-of-chain marker values
    return cluster >= 2 && cluster < 0x0FFFFFF0;
}

static __hint_inline__ int fat32_is_eoc(u32 cluster) {
    return cluster >= 0x0FFFFFF8;
}

static u8 fat32_lfn_checksum(const u8 name[11]) {
    u8 sum = 0;

    for (u32 i = 0; i < 11; ++i) {
        sum = (u8)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]);
    }

    return sum;
}

static __hint_inline__ void fat32_reset_lfn(FAT32_DIR* dir) {
    dir->lfn_count = 0;
    dir->lfn_checksum = 0;
    memset(dir->lfn_parts, 0, sizeof(dir->lfn_parts));
}

static b8 fat32_advance_directory_cluster(FAT32_DIR* dir) {
    u32 next = fat32_read_fat_entry(dir->fs, dir->cluster);

    if (fat32_is_eoc(next)) {
        dir->end = 1;
        return true; // TODO: really not sure what to return???
    }

    if (!fat32_is_data_cluster(next)) {
        dir->end = -1;
        return false; // TODO: really not sure what to return???
    }

    dir->cluster = next;
    dir->offset = 0;
    if (!fat32_read_cluster(dir->fs, dir->cluster, dir->buffer)) {
        dir->end = -1;
        return false;
    }

    return true;
}

static int fat32_stricmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;

        if (ca != cb) return 0;
    }

    return *a == 0 && *b == 0;
}

int fat32_init(FAT32_FS* fs, block_device* dev, u32 part_lba) {
    u8 sector[512];

    if (!dev->read(dev->driver_data, part_lba, 1, sector)) {
        return 0;
    }

    FAT32_BPB* bpb = (FAT32_BPB*)sector;

    if (bpb->bytes_per_sector != 512) {
        serial_write_str("Unsupported sector size\n");
        return 0;
    }

    fs->dev = dev;

    fs->fat_cache_valid = false;
    fs->fat_cache_lba = 0;

    fs->start_lba = part_lba;

    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->root_cluster = bpb->root_cluster;

    u32 fat_size = bpb->fat_size_32;

    fs->fat_start_lba = part_lba + bpb->reserved_sector_count;

    fs->data_start_lba = fs->fat_start_lba + (bpb->fat_count * fat_size);

    serial_write_str("FAT32 OK\n");
    return 1;
}

u32 fat32_cluster_to_lba(FAT32_FS* fs, u32 cluster) {
    return fs->data_start_lba + (cluster - 2) * fs->sectors_per_cluster;
}

u32 fat32_read_fat_entry(FAT32_FS* fs, u32 cluster) {
    u32 next_cluster;

    if (!fat32_read_fat_entry_checked(fs, cluster, &next_cluster)) {
        return 0; // invalid cluster, must reject
    }

    return next_cluster;
}

/*
b8 fat32_read_cluster(FAT32_FS* fs, u32 cluster, void* buffer) {
    u32 lba = fat32_cluster_to_lba(fs, cluster);
    debug_data_cluster_reads++;
    return (b8)fs->dev->read(fs->dev->driver_data, lba, fs->sectors_per_cluster, buffer);
}
    */
b8 fat32_read_cluster(FAT32_FS* fs, u32 cluster, void* buffer) {
    u32 lba = fat32_cluster_to_lba(fs, cluster);

    u64 start = ktimer_get_system_time_in_nanoseconds();

    b8 ok = (b8)fs->dev->read(
        fs->dev->driver_data,
        lba,
        fs->sectors_per_cluster,
        buffer
    );

    u64 end = ktimer_get_system_time_in_nanoseconds();

    debug_cluster_read_ns += end - start;
    debug_data_cluster_reads++;

    return ok;
}

u32 fat32_entry_cluster(FAT32_DIRECTORY_ENTRY* ent) {
    return ((u32)ent->cluster_high << 16) | ent->cluster_low;
}

void fat32_format_8_3(u8* name, char* out) {
    int pos = 0;

    // name (first 8)
    for (int i = 0; i < 8; i++) {
        if (name[i] == ' ') break;
        out[pos++] = name[i];
    }

    // extension
    if (name[8] != ' ') {
        out[pos++] = '.';

        for (int i = 8; i < 11; i++) {
            if (name[i] == ' ') break;
            out[pos++] = name[i];
        }
    }

    out[pos] = 0;
}

int fat32_name_match_8_3(const char* input, u8* name) {
    char fat_name[11];
    memset(fat_name, ' ', 11);

    int i = 0, j = 0;

    // copy name
    while (input[i] && input[i] != '.' && j < 8) {
        fat_name[j++] = input[i++];
    }

    if (input[i] == '.') {
        i++;
        j = 8;
        int k = 0;
        while (input[i] && k < 3) {
            fat_name[j++] = input[i++];
            ++k; // TODO: check, I think this is right
        }
    }

    return memcmp(fat_name, name, 11) == 0;
}

FAT32_FILE* fat32_open(FAT32_FS* fs, const char* path) {
    char component[256];
    int i = 0;

    u32 current_cluster = fs->root_cluster;
    FAT32_DIRECTORY_ENTRY ent;
    memset(&ent, 0, sizeof(FAT32_DIRECTORY_ENTRY));

    // skip leading '/'
    while (path[i] == '/') {
        i++;
    }

    while (1) {
        // extract next path component
        int j = 0;
        while (path[i] && path[i] != '/') {
            component[j++] = path[i++];
        }
        component[j] = 0;

        if (j == 0) break;

        if (!fat32_find_in_dir(fs, current_cluster, component, &ent)) {
            return NULL;
        }

        current_cluster = fat32_entry_cluster(&ent);

        if (path[i] == '/') i++;
        else break;
    }

    FAT32_FILE* f = kmalloc(sizeof(FAT32_FILE));
    if (!f) { return NULL; }
    f->fs = fs;
    f->first_cluster = fat32_entry_cluster(&ent);
    f->size = ent.size;
    f->pos = 0;
    u32 cluster_size = f->fs->sectors_per_cluster * f->fs->bytes_per_sector;
    if (cluster_size == 0) {
        kfree(f);
        return NULL;
    }

    f->cluster_buffer = kmalloc(cluster_size);
    if (!f->cluster_buffer) {
        kfree(f);
        return NULL;
    }

    f->current_cluster = f->first_cluster;
    f->current_cluster_index = 0;
    f->cluster_cursor_valid = true;

    return f;
}

#include <xlibc/stdio.h>

void fat32_close(FAT32_FILE* f) {
    if (!f) return;

    if (f->cluster_buffer) {
        kfree(f->cluster_buffer);
    }

    // printf(
    //     "Totals: FAT sectors=%llu, data clusters=%llu, "
    //     "FAT cache hits=%llu, device read time=%llu ms\n",
    //     debug_fat_sector_reads,
    //     debug_data_cluster_reads,
    //     debug_fat_cache_hits,
    //     debug_cluster_read_ns / 1000000
    // );

    kfree(f);
}

u32 fat32_read(FAT32_FILE* f, void* buffer, u32 size) {
    if (!f || !buffer || size == 0) {
        return 0;
    }

    if (f->pos >= f->size) {
        return 0;
    }

    // clamp read
    if (size > f->size - f->pos) {
        size = f->size - f->pos;
    }

    u8* out = buffer;
    u32 total_read = 0;

    u32 cluster_size = f->fs->sectors_per_cluster * f->fs->bytes_per_sector;

    if (cluster_size == 0 || !f->cluster_buffer) {
        return 0;
    }

    while (total_read < size) {
        // find the cluster containing the current file position
        u32 cluster_index = f->pos / cluster_size;
        u32 cluster;

        if (!fat32_get_cluster_at(f, cluster_index, &cluster)) {
            break;
        }

        // reject invalid clusters before reading from the device
        if (!fat32_is_data_cluster(cluster)) {
            f->cluster_cursor_valid = false;
            break;
        }

        // read this cluster into the reusable buffer
        // only read from the device if this cluster isn't already cached
        if (!f->cluster_buffer_valid || f->loaded_cluster != cluster) {
            if (!fat32_read_cluster(f->fs, cluster, f->cluster_buffer)) {
                f->cluster_buffer_valid = false;
                break;
            }

            f->loaded_cluster = cluster;
            f->cluster_buffer_valid = true;
        }

        u32 cluster_offset = f->pos % cluster_size;
        u32 bytes_available = cluster_size - cluster_offset;
        u32 bytes_to_copy = size - total_read;

        if (bytes_to_copy > bytes_available) {
            bytes_to_copy = bytes_available;
        }

        memcpy(out + total_read, f->cluster_buffer + cluster_offset, bytes_to_copy);

        f->pos += bytes_to_copy;
        total_read += bytes_to_copy;
    }

    return total_read;
}

int fat32_find_in_dir(FAT32_FS* fs, u32 start_cluster, const char* name, FAT32_DIRECTORY_ENTRY* out) {
    u32 cluster = start_cluster;
    u32 cluster_size = fs->sectors_per_cluster * 512;

    u8* buf = kmalloc(cluster_size);

    char lfn_parts[MAX_LFN_PARTS][14]; // each entry max 13 chars + null
    int lfn_count = 0;

    while (cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(fs, cluster, buf)) {
            kfree(buf);
            return 0;
        }

        for (u32 i = 0; i < cluster_size; i += 32) {
            FAT32_DIRECTORY_ENTRY* ent = (void*)(buf + i);

            if (ent->name[0] == 0x00) { kfree(buf); return 0; }
            if (ent->name[0] == 0xE5) continue;

            // LFN entry
            if (ent->attr == 0x0F) {
                FAT32_LFN_ENTRY* lfn = (void*)ent;

                int order = lfn->order & 0x1F;

                if (order > 0 && order <= MAX_LFN_PARTS) {
                    fat32_lfn_extract(lfn, &lfn_parts[order - 1][0]);

                    if (lfn->order & 0x40) {
                        lfn_count = order;
                    }
                }

                continue;
            }

            // build final name
            char final_name[256];
            final_name[0] = 0;

            if (lfn_count > 0) {
                int pos = 0;

                for (int k = 0; k < lfn_count; k++) {
                    int j = 0;
                    while (lfn_parts[k][j]) {
                        final_name[pos++] = lfn_parts[k][j++];
                    }
                }

                final_name[pos] = 0;
            } else {
                // fallback to 8.3
                memcpy(final_name, ent->name, 11);
                final_name[11] = 0;
            }

            // match names
            char short_name[32];
            fat32_format_8_3(ent->name, short_name);

            int match = 0;

            if (lfn_count > 0) {
                if (fat32_stricmp(final_name, name)) {
                    match = 1;
                }
            }

            // fallback to 8.3 always
            if (!match) {
                if (fat32_stricmp(short_name, name)) {
                    match = 1;
                }
            }

            if (match) {
                memcpy(out, ent, sizeof(*out));
                kfree(buf);
                return 1;
            }

            // reset for next entry
            lfn_count = 0;
            memset(lfn_parts, 0, sizeof(lfn_parts));
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    kfree(buf);
    return 0;
}

void fat32_lfn_extract(FAT32_LFN_ENTRY* lfn, char* out) {
    int pos = 0;

    for (int i = 0; i < 5; i++) {
        if (lfn->name1[i] == 0x0000 || lfn->name1[i] == 0xFFFF) break;
        out[pos++] = (char)lfn->name1[i];
    }
    for (int i = 0; i < 6; i++) {
        if (lfn->name2[i] == 0x0000 || lfn->name2[i] == 0xFFFF) break;
        out[pos++] = (char)lfn->name2[i];
    }
    for (int i = 0; i < 2; i++) {
        if (lfn->name3[i] == 0x0000 || lfn->name3[i] == 0xFFFF) break;
        out[pos++] = (char)lfn->name3[i];
    }

    out[pos] = 0;
}

u32 fat32_get_size(FAT32_FILE* file) {
    return file->size;
}

int fat32_seek(FAT32_FILE* f, u32 offset) {
    if (!f) return -1;

    if (offset > f->size) {
        offset = f->size; // clamp
    }

    f->pos = offset;
    return 0;
}

u32 fat32_tell(FAT32_FILE* f) {
    return f->pos;
}

FAT32_DIR* fat32_opendir(FAT32_FS* fs, const char* path) {
    if (!fs || !fs->dev || !path) {
        return NULL;
    }

    u32 current_cluster = fs->root_cluster;
    int i = 0;

    // accept "/", repeated leading slashes, and an empty path as root
    while (path[i] == '/') {
        ++i;
    }

    while (path[i] != '\0') {
        char component[256];
        u32 length = 0;

        while (path[i] != '\0' && path[i] != '/') {
            if (length >= sizeof(component) - 1) {
                return NULL;
            }

            component[length++] = path[i++];
        }

        component[length] = '\0';

        while (path[i] == '/') {
            ++i;
        }

        // ignore empty components caused by repeated slashes
        if (length == 0) {
            continue;
        }

        FAT32_DIRECTORY_ENTRY entry;

        if (!fat32_find_in_dir( fs, current_cluster, component, &entry)) {
            return NULL;
        }

        // every component in this path must be a directory
        if (!(entry.attr & 0x10) || (entry.attr & 0x08)) {
            return NULL;
        }

        current_cluster = fat32_entry_cluster(&entry);

        if (!fat32_is_data_cluster(current_cluster)) {
            return NULL;
        }
    }

    FAT32_DIR* dir = kmalloc(sizeof(FAT32_DIR));
    if (!dir) {
        return NULL;
    }

    memset(dir, 0, sizeof(FAT32_DIR));

    dir->fs = fs;
    dir->cluster = current_cluster;
    dir->offset = 0;
    dir->cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    dir->end = 0;

    if (dir->cluster_size == 0) {
        kfree(dir);
        return NULL;
    }

    dir->buffer = kmalloc(dir->cluster_size);
    if (!dir->buffer) {
        kfree(dir);
        return NULL;
    }

    if (!fat32_read_cluster(fs, dir->cluster, dir->buffer)) {
        kfree(dir);
        return NULL;
    }
    
    fat32_reset_lfn(dir);

    return dir;
}

int fat32_readdir(FAT32_DIR* dir, VFS_DIRENT* out)
{
    if (!dir || !out || !dir->fs || !dir->buffer || dir->end < 0) { return -1; }
    if (dir->end == 1) { return 0; }

    for (;;) {
        // move to the next cluster when this one is exhausted
        if (dir->offset >= dir->cluster_size) {
            if (!fat32_advance_directory_cluster(dir)) {
                return -1;
            }

            if (dir->end == 1) {
                return 0;
            }

            if (dir->end < 0) {
                return -1;
            }
        }

        FAT32_DIRECTORY_ENTRY entry;
        memcpy(&entry, dir->buffer + dir->offset, sizeof(entry));
        dir->offset += sizeof(entry);

        // a zero first byte marks the end of the directory
        if (entry.name[0] == 0x00) {
            dir->end = 1;
            return 0;
        }

        // deleted entries invalidate any pending LFN sequence
        if (entry.name[0] == 0xE5) {
            fat32_reset_lfn(dir);
            continue;
        }

        if (entry.attr == 0x0F) {
            FAT32_LFN_ENTRY lfn;
            memcpy(&lfn, &entry, sizeof(lfn));

            u32 order = lfn.order & 0x1F;

            if (lfn.order & 0x40) {
                // this is the first on-disk record of a new LFN
                fat32_reset_lfn(dir);

                if (order == 0 || order > MAX_LFN_PARTS) {
                    continue;
                }

                dir->lfn_count = order;
                dir->lfn_checksum = lfn.checksum;
            }

            if (dir->lfn_count == 0 || order == 0 || order > dir->lfn_count || lfn.checksum != dir->lfn_checksum) { fat32_reset_lfn(dir);
                continue;
            }

            fat32_lfn_extract(&lfn, dir->lfn_parts[order - 1]);
            continue;
        }

        // volume labels are not files or directories
        if (entry.attr & 0x08) {
            fat32_reset_lfn(dir);
            continue;
        }

        char short_name[32];
        fat32_format_8_3(entry.name, short_name);

        char name[VFS_NAME_MAX + 1];
        name[0] = '\0';

        // use the long name only if its checksum matches the short entry
        if (dir->lfn_count != 0 && dir->lfn_checksum == fat32_lfn_checksum(entry.name)) {
            u32 pos = 0;

            for (u32 part = 0; part < dir->lfn_count && pos < VFS_NAME_MAX; ++part) {
                for (u32 j = 0; dir->lfn_parts[part][j] != '\0' && pos < VFS_NAME_MAX; ++j) {
                    name[pos++] = dir->lfn_parts[part][j];
                }
            }

            name[pos] = '\0';
        }

        // fall back to the 8.3 alias if there was no usable LFN
        if (name[0] == '\0') {
            u32 i = 0;

            while (short_name[i] != '\0' && i < VFS_NAME_MAX) {
                name[i] = short_name[i];
                ++i;
            }

            name[i] = '\0';
        }

        fat32_reset_lfn(dir);

        memset(out, 0, sizeof(*out));
        memcpy(out->name, name, strlen(name) + 1);
        out->size = entry.size;
        out->type = (entry.attr & 0x10) ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;

        return 1;
    }
}

void fat32_closedir(FAT32_DIR* dir) {
    if (!dir) { return; }

    if (dir->buffer) {
        kfree(dir->buffer);
    }

    kfree(dir);
}