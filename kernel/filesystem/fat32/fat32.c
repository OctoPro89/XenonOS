#include "fat32.h"
#include <arch/x86_64/io.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <xlibc/stdlib.h>

#define MAX_LFN_PARTS 20

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
    u32 fat_offset = cluster * 4;

    u32 fat_sector = fs->fat_start_lba + (fat_offset / 512);
    u32 ent_offset = fat_offset % 512;

    u8 sector[512];
    fs->dev->read(fs->dev->driver_data, fat_sector, 1, sector);

    u32 value = *(u32*)(sector + ent_offset);

    return value & 0x0FFFFFFF;
}

void fat32_read_cluster(FAT32_FS* fs, u32 cluster, void* buffer) {
    u32 lba = fat32_cluster_to_lba(fs, cluster);
    fs->dev->read(fs->dev->driver_data, lba, fs->sectors_per_cluster, buffer);
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
        }
    }

    return memcmp(fat_name, name, 11) == 0;
}

FAT32_FILE* fat32_open(FAT32_FS* fs, const char* path) {
    char component[256];
    int i = 0;

    u32 current_cluster = fs->root_cluster;
    FAT32_DIRECTORY_ENTRY ent;

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

    FAT32_FILE* f = malloc(sizeof(FAT32_FILE));
    f->fs = fs;
    f->first_cluster = fat32_entry_cluster(&ent);
    f->size = ent.size;
    f->pos = 0;
    u32 cluster_size = f->fs->sectors_per_cluster * 512;
    f->cluster_buffer = malloc(cluster_size);

    return f;
}

void fat32_close(FAT32_FILE* f) {
    free(f);
}

u32 fat32_read(FAT32_FILE* f, void* buffer, u32 size) {
    u8* out = buffer;
    u32 total_read = 0;

    u32 cluster_size = f->fs->sectors_per_cluster * 512;

    // clamp to file size
    if (f->pos >= f->size) return 0;
    if (f->pos + size > f->size) {
        size = f->size - f->pos;
    }

    // --- find starting cluster ---
    u32 cluster = f->first_cluster;
    u32 cluster_index = f->pos / cluster_size;

    for (u32 i = 0; i < cluster_index; i++) {
        cluster = fat32_read_fat_entry(f->fs, cluster);
        if (cluster >= 0x0FFFFFF8) return 0;
    }

    u32 cluster_offset = f->pos % cluster_size;

    // use pre-allocated buffer for speed, could malloc a temp here
    uint8_t* temp = f->cluster_buffer;

    while (cluster < 0x0FFFFFF8 && total_read < size) {
        fat32_read_cluster(f->fs, cluster, temp);

        u32 to_copy = cluster_size - cluster_offset;

        if (to_copy > (size - total_read)) {
            to_copy = size - total_read;
        }

        memcpy(out + total_read, temp + cluster_offset, to_copy);

        total_read += to_copy;
        f->pos += to_copy;

        cluster_offset = 0; // only first cluster has offset

        cluster = fat32_read_fat_entry(f->fs, cluster);
    }

    return total_read;
}

int fat32_find_in_dir(FAT32_FS* fs, u32 start_cluster, const char* name, FAT32_DIRECTORY_ENTRY* out) {
    u32 cluster = start_cluster;
    u32 cluster_size = fs->sectors_per_cluster * 512;

    u8* buf = malloc(cluster_size);

    char lfn_parts[MAX_LFN_PARTS][14]; // each entry max 13 chars + null
    int lfn_count = 0;

    while (cluster < 0x0FFFFFF8) {
        fat32_read_cluster(fs, cluster, buf);

        for (u32 i = 0; i < cluster_size; i += 32) {
            FAT32_DIRECTORY_ENTRY* ent = (void*)(buf + i);

            if (ent->name[0] == 0x00) { free(buf); return 0; }
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
                free(buf);
                return 1;
            }

            // reset for next entry
            lfn_count = 0;
            memset(lfn_parts, 0, sizeof(lfn_parts));
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    free(buf);
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