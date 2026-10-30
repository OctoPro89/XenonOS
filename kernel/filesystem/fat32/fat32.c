#include "fat32.h"
#include <arch/x86_64/io.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <memory/heap.h>
#include <kernel.h>

#define FAT32_LFN_MAX_ENTRIES 20
#define FAT32_CREATE_MAX_ENTRIES (FAT32_LFN_MAX_ENTRIES + 1)

typedef struct {
    u32 cluster;
    u32 offset;
} FAT32_DIR_SLOT;

#define FAT32_EOC 0x0FFFFFFF

static b8 fat32_update_file_entry(FAT32_FILE* f);
static b8 fat32_allocate_cluster(FAT32_FS* fs, u32* out_cluster);
static b8 fat32_get_cluster_for_write(FAT32_FILE* f, u32 target_index, u32* out_cluster);
static b8 fat32_get_cluster_at(FAT32_FILE *f, u32 target_index, u32 *out_cluster);
static b8 fat32_write_fat_entry(FAT32_FS* fs, u32 cluster, u32 value);
static u8 fat32_lfn_checksum(const u8 name[11]);
static int fat32_stricmp(const char* a, const char* b);

static __hint_inline__ int fat32_is_eoc(u32 cluster) {
    return cluster >= 0x0FFFFFF8;
}

static __hint_inline__ int fat32_is_data_cluster(u32 cluster) {
    // exclude free, reserved, bad, and end-of-chain marker values
    return cluster >= 2 && cluster < 0x0FFFFFF0;
}

static __hint_inline__ void fat32_reset_lfn(FAT32_DIR* dir) {
    dir->lfn_count = 0;
    dir->lfn_checksum = 0;
    memset(dir->lfn_parts, 0, sizeof(dir->lfn_parts));
}

static b8 fat32_update_file_entry(FAT32_FILE* f) {
    if (!f || !f->fs || !f->fs->dev || !f->fs->dev->read || !f->fs->dev->write) {
        return false;
    }

    if (f->dir_entry_offset + sizeof(FAT32_DIRECTORY_ENTRY) > 512) {
        return false;
    }

    u8 sector[512];

    if (!f->fs->dev->read(f->fs->dev->driver_data, f->dir_entry_lba, 1, sector)) {
        return false;
    }

    FAT32_DIRECTORY_ENTRY* ent = (FAT32_DIRECTORY_ENTRY*)(sector + f->dir_entry_offset);

    ent->cluster_high = (u16)((f->first_cluster >> 16) & 0x0FFF);
    ent->cluster_low = (u16)(f->first_cluster & 0xFFFF);
    ent->size = f->size;

    if (!f->fs->dev->write(f->fs->dev->driver_data, f->dir_entry_lba, 1, sector)) {
        return false;
    }

    return true;
}

static b8 fat32_allocate_cluster(FAT32_FS* fs, u32* out_cluster) {
    if (!fs || !out_cluster || !fs->dev || !fs->dev->write) {
        return false;
    }

    u32 max_cluster = fs->cluster_count + 1;

    for (u32 cluster = 2; cluster <= max_cluster; ++cluster) {
        u32 value;

        if (!fat32_read_fat_entry(fs, cluster, &value)) {
            return false;
        }

        if (value != 0) {
            continue;
        }

        // mark the cluster as allocated and terminate its chain
        if (!fat32_write_fat_entry(fs, cluster, FAT32_EOC)) {
            return false;
        }

        u32 cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
        u8* zero_buffer = kmalloc(cluster_size);

        if (!zero_buffer) {
            // roll back the allocation if initialization fails
            fat32_write_fat_entry(fs, cluster, 0);
            return false;
        }

        memset(zero_buffer, 0, cluster_size);

        b8 ok = fat32_write_cluster(fs, cluster, zero_buffer);

        kfree(zero_buffer);

        if (!ok) {
            fat32_write_fat_entry(fs, cluster, 0);
            return false;
        }

        *out_cluster = cluster;
        return true;
    }

    return false; // disk full
}

static b8 fat32_get_cluster_for_write(FAT32_FILE* f, u32 target_index, u32* out_cluster) {
    if (!f || !f->fs || !out_cluster) {
        return false;
    }

    // an empty file has no allocated data cluster yet
    if (f->first_cluster == 0) {
        u32 first;

        if (!fat32_allocate_cluster(f->fs, &first)) {
            return false;
        }

        f->first_cluster = first;
        f->current_cluster = first;
        f->current_cluster_index = 0;
        f->cluster_cursor_valid = true;
    }

    u32 cluster;

    if (!fat32_get_cluster_at(f, target_index, &cluster)) {
        // the read helper cannot traverse past EOC, walk the chain below instead, allocating as needed
        cluster = f->first_cluster;
        f->current_cluster_index = 0;
        f->current_cluster = cluster;
        f->cluster_cursor_valid = true;
    }

    while (f->current_cluster_index < target_index) {
        u32 next;

        if (!fat32_read_fat_entry(f->fs, f->current_cluster, &next)) {
            f->cluster_cursor_valid = false;
            return false;
        }

        if (fat32_is_eoc(next)) {
            if (!fat32_allocate_cluster(f->fs, &next)) {
                f->cluster_cursor_valid = false;
                return false;
            }

            if (!fat32_write_fat_entry(f->fs, f->current_cluster, next)) {
                f->cluster_cursor_valid = false;
                return false;
            }
        } else if (!fat32_is_data_cluster(next)) {
            f->cluster_cursor_valid = false;
            return false;
        }

        f->current_cluster = next;
        ++f->current_cluster_index;
    }

    *out_cluster = f->current_cluster;
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

        if (!fat32_read_fat_entry(f->fs, f->current_cluster, &next_cluster)) {
            f->cluster_cursor_valid = false;
            return false;
        }

        if (next_cluster < 2 || next_cluster >= 0x0FFFFFF0) {
            f->cluster_cursor_valid = false;
            return false;
        }

        f->current_cluster = next_cluster;
        ++f->current_cluster_index;
    }

    *out_cluster = f->current_cluster;
    return true;
}

// TODO: honor BPB's ext_flags
static b8 fat32_write_fat_entry(FAT32_FS* fs, u32 cluster, u32 value) {
    if (!fs || !fs->dev || !fs->dev->write) {
        return false;
    }

    if (cluster >= fs->cluster_count + 2) {
        return false;
    }

    u32 fat_offset = cluster * 4;
    u32 sector_offset = fat_offset / 512;
    u32 entry_offset = fat_offset % 512;

    if (sector_offset >= fs->fat_size_sectors) {
        return false;
    }

    u32 fat_count = fs->fat_count;
    if (fat_count == 0) {
        return false;
    }

    // update each FAT copy, this implementation assumes the standard mirrored-FAT configuration
    for (u32 copy = 0; copy < fat_count; ++copy) {
        u32 lba = fs->fat_start_lba + copy * fs->fat_size_sectors + sector_offset;

        u8 sector[512];

        // reuse the cached sector for the first FAT when possible
        if (copy == 0 && fs->fat_cache_valid && fs->fat_cache_lba == lba) {
            memcpy(sector, fs->fat_cache, sizeof(sector));
        } else {
            if (!fs->dev->read(fs->dev->driver_data, lba, 1, sector)) {
                return false;
            }
        }

        u32 old_value;
        memcpy(&old_value, &sector[entry_offset], sizeof(old_value));

        u32 new_value = (old_value & 0xF0000000) | (value & 0x0FFFFFFF);

        memcpy(&sector[entry_offset], &new_value, sizeof(new_value));

        if (!fs->dev->write( fs->dev->driver_data, lba, 1, sector)) {
            fs->fat_cache_valid = false;
            return false;
        }

        if (copy == 0) {
            memcpy(fs->fat_cache, sector, sizeof(sector));
            fs->fat_cache_lba = lba;
            fs->fat_cache_valid = true;
        }
    }

    return true;
}

static u8 fat32_lfn_checksum(const u8 name[11]) {
    u8 sum = 0;

    for (u32 i = 0; i < 11; ++i) {
        sum = (u8)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]);
    }

    return sum;
}

static b8 fat32_advance_directory_cluster(FAT32_DIR* dir) {
    u32 next = 0;
    if (!fat32_read_fat_entry(dir->fs, dir->cluster, &next)) {
        return false;
    }

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

static b8 fat32_make_short_name(const char* input, u8 out[11]) {
    if (!input || !*input) {
        return false;
    }

    memset(out, ' ', 11);

    u32 base_len = 0;
    u32 ext_len = 0;
    b8 in_extension = false;

    for (u32 i = 0; input[i]; ++i) {
        char c = input[i];

        if (c == '.') {
            if (in_extension || base_len == 0) {
                return false;
            }

            in_extension = true;
            continue;
        }

        // keep this implementation ASCII-only and reject characters forbidden in FAT short filenames.
        if (c <= ' ' || c >= 127 ||
            c == '/' || c == '\\' || c == '*' || c == '?' ||
            c == '"' || c == ':' || c == '<' || c == '>' ||
            c == '|' || c == '+' || c == ',' || c == ';' ||
            c == '=' || c == '[' || c == ']') {
            return false;
        }

        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }

        if (!in_extension) {
            if (base_len >= 8) {
                return false;
            }

            out[base_len++] = (u8)c;
        } else {
            if (ext_len >= 3) {
                return false;
            }

            out[8 + ext_len++] = (u8)c;
        }
    }

    if (base_len == 0 || (in_extension && ext_len == 0)) {
        return false;
    }

    return true;
}

/**
 * @brief Inserts directory entries into an existing directory.
 * @note If needed, extends the directory by allocating and linking a cluster.
 */
static b8 fat32_insert_directory_entries(FAT32_FS* fs, u32 start_cluster, const void* records, u32 count, u32* out_lba, u16* out_offset) {
    if (!fs || !records || !out_lba || !out_offset ||
        count == 0 || count > FAT32_CREATE_MAX_ENTRIES) {
        return false;
    }

    u32 cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;

    if (cluster_size < 32 || cluster_size % 32 != 0 || start_cluster < 2 || start_cluster >= fs->cluster_count + 2) {
        return false;
    }

    FAT32_DIR_SLOT slots[FAT32_CREATE_MAX_ENTRIES];
    u8* buf = kmalloc(cluster_size);

    if (!buf) {
        return false;
    }

    u32 cluster = start_cluster;
    u32 run = 0;
    u32 visited = 0;

    b8 end_seen = false;
    b8 run_saw_end = false;
    b8 used_end_marker = false;
    b8 found = false;

    const u8* data = (const u8*)records;

    while (visited < fs->cluster_count) {
        if (cluster < 2 ||
            cluster >= fs->cluster_count + 2) {
            break;
        }

        if (!fat32_read_cluster(fs, cluster, buf)) {
            kfree(buf);
            return false;
        }

        for (u32 off = 0; off + 32 <= cluster_size; off += 32) {
            u8 first = buf[off];
            b8 at_end = (first == 0x00);

            if (at_end) {
                end_seen = true;
            }

            // once 0x00 is encountered, all subsequent entries are logically unused, even if their bytes contain old data
            b8 free_slot = end_seen || first == 0xE5;

            if (!free_slot) {
                run = 0;
                run_saw_end = false;
                continue;
            }

            if (run == 0) {
                run_saw_end = false;
            }

            slots[run].cluster = cluster;
            slots[run].offset = off;

            if (at_end) {
                run_saw_end = true;
            }

            ++run;

            if (run == count) {
                found = true;
                used_end_marker = run_saw_end;
                break;
            }
        }

        if (found) {
            break;
        }

        u32 next;

        if (!fat32_read_fat_entry(fs, cluster, &next)) {
            kfree(buf);
            return false;
        }

        if (fat32_is_eoc(next)) {
            // No contiguous run was found so extend the directory, the new cluster is zero-initialized by the allocator
            u32 new_cluster;

            if (!fat32_allocate_cluster(fs, &new_cluster)) {
                kfree(buf);
                return false;
            }

            if (!fat32_write_fat_entry(fs, cluster, new_cluster)) {
                fat32_write_fat_entry(fs, new_cluster, 0);
                kfree(buf);
                return false;
            }

            cluster = new_cluster;
        } else {
            if (!fat32_is_data_cluster(next) ||
                next >= fs->cluster_count + 2) {
                kfree(buf);
                return false;
            }

            cluster = next;
        }

        ++visited;
    }

    if (!found) {
        kfree(buf);
        return false;
    }

    // If the directory's 0x00 end marker was consumed, preserve a new marker immediately after the inserted records, if the records fill the final cluster, EOC already terminates the directory
    FAT32_DIR_SLOT last = slots[count - 1];

    b8 marker_exists = false;
    u32 marker_cluster = 0;
    u32 marker_offset = 0;

    if (used_end_marker) {
        if (last.offset + 32 < cluster_size) {
            marker_exists = true;
            marker_cluster = last.cluster;
            marker_offset = last.offset + 32;
        } else {
            u32 next;

            if (!fat32_read_fat_entry(fs, last.cluster, &next)) {
                kfree(buf);
                return false;
            }

            if (!fat32_is_eoc(next)) {
                if (!fat32_is_data_cluster(next) ||
                    next >= fs->cluster_count + 2) {
                    kfree(buf);
                    return false;
                }

                marker_exists = true;
                marker_cluster = next;
                marker_offset = 0;
            }
        }
    }

    // write consecutive records, one cluster at a time, each record is exactly 32 bytes.
    u32 i = 0;

    while (i < count) {
        u32 write_cluster = slots[i].cluster;

        if (!fat32_read_cluster(fs, write_cluster, buf)) {
            kfree(buf);
            return false;
        }

        while (i < count && slots[i].cluster == write_cluster) {
            memcpy(buf + slots[i].offset, data + i * 32, 32);

            ++i;
        }

        if (marker_exists && marker_cluster == write_cluster) {
            buf[marker_offset] = 0x00;
        }

        if (!fat32_write_cluster(fs, write_cluster, buf)) {
            kfree(buf);
            return false;
        }
    }

    // the end marker may be in the cluster immediately following the last record, update that cluster if necessary
    if (marker_exists && marker_cluster != last.cluster) {
        if (!fat32_read_cluster(fs, marker_cluster, buf)) {
            kfree(buf);
            return false;
        }

        buf[marker_offset] = 0x00;

        if (!fat32_write_cluster(fs, marker_cluster, buf)) {
            kfree(buf);
            return false;
        }
    }

    // the final record is the short-name directory entry
    *out_lba = fat32_cluster_to_lba(fs, last.cluster) + last.offset / fs->bytes_per_sector;

    *out_offset = (u16)(last.offset % fs->bytes_per_sector);

    kfree(buf);
    return true;
}

static b8 fat32_validate_lfn_ascii(
    const char* name,
    u32* out_length
) {
    if (!name || !out_length) {
        return false;
    }

    u32 len = (u32)strnlen(name, 256);

    if (len == 0 || len > 255) {
        return false;
    }

    if ((len == 1 && name[0] == '.') ||
        (len == 2 && name[0] == '.' && name[1] == '.')) {
        return false;
    }

    if (name[0] == ' ' ||
        name[len - 1] == ' ' ||
        name[len - 1] == '.') {
        return false;
    }

    for (u32 i = 0; i < len; ++i) {
        u8 c = (u8)name[i];

        // ASCII-only initially; add UTF-8 decoding separately.
        if (c < 0x20 || c >= 0x7F) {
            return false;
        }

        if (c == '"' || c == '*' || c == '/' ||
            c == ':' || c == '<' || c == '>' ||
            c == '?' || c == '\\' || c == '|') {
            return false;
        }
    }

    *out_length = len;
    return true;
}


static void fat32_lfn_set_unit(FAT32_LFN_ENTRY* ent, u32 index, u16 value) {
    if (index < 5) {
        ent->name1[index] = value;
    } else if (index < 11) {
        ent->name2[index - 5] = value;
    } else {
        ent->name3[index - 11] = value;
    }
}

static b8 fat32_build_lfn_entries(const char* name, const u8 short_name[11], FAT32_LFN_ENTRY out[FAT32_LFN_MAX_ENTRIES], u32* out_count) {
    u32 len;

    if (!short_name || !out || !out_count || !fat32_validate_lfn_ascii(name, &len)) {
        return false;
    }

    u32 count = (len + 12) / 13;

    if (count == 0 || count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    u8 checksum = fat32_lfn_checksum(short_name);

    // entries are stored physically in reverse order: N|0x40, N-1, ..., 2, 1.
    for (u32 physical = 0; physical < count; ++physical) {
        u32 order = count - physical;

        FAT32_LFN_ENTRY* ent = &out[physical];
        memset(ent, 0, sizeof(*ent));

        ent->order = (u8)order;

        if (physical == 0) {
            ent->order |= 0x40;
        }

        ent->attr = 0x0F;
        ent->type = 0;
        ent->checksum = checksum;
        ent->first_cluster_low = 0;

        // each entry contains 13 UTF-16 code units, ASCII is identical in UTF-8 and UTF-16 for these characters
        for (u32 j = 0; j < 13; ++j) {
            u32 char_index = (order - 1) * 13 + j;
            u16 value;

            if (char_index < len) {
                value = (u8)name[char_index];
            } else if (char_index == len) {
                value = 0x0000;
            } else {
                value = 0xFFFF;
            }

            fat32_lfn_set_unit(ent, j, value);
        }
    }

    *out_count = count;
    return true;
}

static b8 fat32_generate_short_alias(FAT32_FS* fs, u32 parent_cluster, const char* name, u8 out[11]) {
    if (!fs || !name || !out) {
        return false;
    }

    u32 len = (u32)strlen(name);
    const char* dot = NULL;

    for (u32 i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = name + i;
        }
    }

    u32 base_source_len = dot ? (u32)(dot - name) : len;

    char base[7];
    u32 base_len = 0;

    for (u32 i = 0; i < base_source_len && base_len < 6; ++i) {
        char c = name[i];

        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }

        b8 alnum = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');

        base[base_len++] = alnum ? c : '_';
    }

    if (base_len == 0) {
        base[base_len++] = 'F';
    }

    char ext[3];
    u32 ext_len = 0;

    if (dot) {
        const char* p = dot + 1;

        while (*p && ext_len < 3) {
            char c = *p++;

            if (c >= 'a' && c <= 'z') {
                c -= 'a' - 'A';
            }

            b8 alnum = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');

            ext[ext_len++] = alnum ? c : '_';
        }
    }

    for (u32 attempt = 1; attempt <= 999999; ++attempt) {
        char digits[7];
        u32 digit_count = 0;
        u32 n = attempt;

        do {
            digits[digit_count++] = (char)('0' + n % 10);
            n /= 10;
        } while (n != 0);

        memset(out, ' ', 11);

        u32 prefix_len = 8 - 1 - digit_count;

        if (prefix_len > base_len) {
            prefix_len = base_len;
        }

        memcpy(out, base, prefix_len);
        out[prefix_len] = '~';

        for (u32 i = 0; i < digit_count; ++i) {
            out[prefix_len + 1 + i] = digits[digit_count - i - 1];
        }

        for (u32 i = 0; i < ext_len; ++i) {
            out[8 + i] = ext[i];
        }

        char alias_text[32];
        fat32_format_8_3(out, alias_text);

        FAT32_DIRECTORY_ENTRY existing;

        if (!fat32_find_in_dir(fs, parent_cluster, alias_text, &existing, NULL, NULL)) {
            return true;
        }
    }

    return false;
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

    fs->fat_count = bpb->fat_count;
    fs->fat_size_sectors = bpb->fat_size_32;

    u32 total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;

    u32 first_data_sector = bpb->reserved_sector_count + ((u32)bpb->fat_count * bpb->fat_size_32);

    if (bpb->sectors_per_cluster == 0 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0 || bpb->fat_count == 0 || bpb->fat_size_32 == 0 || total_sectors <= first_data_sector) {
        return 0;
    }

    u32 data_sectors = total_sectors - first_data_sector;

    fs->cluster_count = data_sectors / bpb->sectors_per_cluster;

    serial_write_str("FAT32 OK\n");
    return 1;
}

u32 fat32_cluster_to_lba(FAT32_FS* fs, u32 cluster) {
    return fs->data_start_lba + (cluster - 2) * fs->sectors_per_cluster;
}

b8 fat32_read_fat_entry(FAT32_FS* fs, u32 cluster, u32* out_cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = fs->fat_start_lba + (fat_offset / 512);
    u32 ent_offset = fat_offset % 512;

    if (fs->fat_cache_valid &&
        fs->fat_cache_lba == fat_sector) {
    }

    if (!fs->fat_cache_valid || fs->fat_cache_lba != fat_sector) {
        if (!fs->dev->read(fs->dev->driver_data, fat_sector, 1, fs->fat_cache)) {
            fs->fat_cache_valid = false;
            return false;
        }

        // ++debug_fat_sector_reads;
        fs->fat_cache_lba = fat_sector;
        fs->fat_cache_valid = true;
    }

    u32 value;
    memcpy(&value, &fs->fat_cache[ent_offset], sizeof(value));

    *out_cluster = value & 0x0FFFFFFF;
    return true;
}

b8 fat32_read_cluster(FAT32_FS* fs, u32 cluster, void* buffer) {
    u32 lba = fat32_cluster_to_lba(fs, cluster);
    return (b8)fs->dev->read(fs->dev->driver_data, lba, fs->sectors_per_cluster, buffer);
}

b8 fat32_write_cluster(FAT32_FS* fs, u32 cluster, const void* buffer) {
    if (!fs || !fs->dev || !fs->dev->write || !buffer) {
        return false;
    }

    if (cluster < 2 || fs->sectors_per_cluster == 0 || fs->bytes_per_sector != 512) {
        return false;
    }

    u32 lba = fat32_cluster_to_lba(fs, cluster);

    return (b8)fs->dev->write(fs->dev->driver_data, lba, fs->sectors_per_cluster, buffer);
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
    if (!fs || !path || !fs->dev) {
        return NULL;
    }

    char component[256];
    const char* p = path;

    while (*p == '/') {
        ++p;
    }

    if (*p == '\0') {
        return NULL;
    }

    u32 current_cluster = fs->root_cluster;
    FAT32_DIRECTORY_ENTRY ent;
    u32 entry_lba = 0;
    u16 entry_offset = 0;
    b8 found_entry = false;

    while (*p != '\0') {
        u32 j = 0;

        while (*p != '\0' && *p != '/') {
            if (j >= sizeof(component) - 1) {
                return NULL;
            }

            component[j++] = *p++;
        }

        component[j] = '\0';

        while (*p == '/') {
            ++p;
        }

        if (j == 0) {
            break;
        }

        if (!fat32_find_in_dir(fs, current_cluster, component, &ent, &entry_lba, &entry_offset)) {
            return NULL;
        }

        found_entry = true;

        if (*p != '\0') {
            if (!(ent.attr & 0x10) || (ent.attr & 0x08)) {
                return NULL;
            }

            current_cluster = fat32_entry_cluster(&ent);

            if (!fat32_is_data_cluster(current_cluster)) {
                return NULL;
            }
        } else {
            if (ent.attr & (0x10 | 0x08)) {
                return NULL;
            }
        }
    }

    if (!found_entry) {
        return NULL;
    }

    FAT32_FILE* f = kmalloc(sizeof(FAT32_FILE));
    if (!f) { return NULL; }
    memset(f, 0, sizeof(FAT32_FILE));
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
    memset(f->cluster_buffer, 0, cluster_size);

    f->current_cluster = f->first_cluster;
    f->current_cluster_index = 0;
    f->cluster_cursor_valid = true;

    f->dir_entry_lba = entry_lba;
    f->dir_entry_offset = entry_offset;

    return f;
}

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

FAT32_FILE* fat32_create(FAT32_FS* fs, const char* path) {
    if (!fs || !path || !fs->dev) {
        return NULL;
    }

    size_t path_len = strnlen(path, 256);

    if (path_len == 0 || path_len >= 256 || path[path_len - 1] == '/') {
        return NULL;
    }

    char path_copy[256];
    memcpy(path_copy, path, path_len + 1);

    // split into parent path and final filename
    char* last_slash = NULL;

    for (size_t i = 0; i < path_len; ++i) {
        if (path_copy[i] == '/') {
            last_slash = &path_copy[i];
        }
    }

    char* basename = path_copy;
    char parent_path[256] = "/";

    if (last_slash) {
        basename = last_slash + 1;

        if (basename[0] == '\0') {
            return NULL;
        }

        if (last_slash != path_copy) {
            *last_slash = '\0';
            memcpy(parent_path, path_copy, strlen(path_copy) + 1);
        }
    }

    // resolve the parent directory
    FAT32_DIR* parent = fat32_opendir(fs, parent_path);

    if (!parent) {
        return NULL;
    }

    u32 parent_cluster = parent->cluster;

    // do not overwrite an existing entry
    FAT32_DIRECTORY_ENTRY existing;

    if (fat32_find_in_dir(fs, parent_cluster, basename, &existing, NULL, NULL)) {
        fat32_closedir(parent);
        return NULL;
    }

    u8 short_name[11];

    if (!fat32_generate_short_alias(fs, parent_cluster, basename, short_name)) {
        fat32_closedir(parent);
        return NULL;
    }

    FAT32_LFN_ENTRY lfn[FAT32_LFN_MAX_ENTRIES];
    u32 lfn_count;

    if (!fat32_build_lfn_entries(basename, short_name, lfn, &lfn_count)) {
        fat32_closedir(parent);
        return NULL;
    }

    FAT32_DIRECTORY_ENTRY entry;
    memset(&entry, 0, sizeof(entry));

    memcpy(entry.name, short_name, sizeof(short_name));
    entry.attr = 0x20;  // archive: regular file

    // minimum valid FAT date: January 1, 1980
    // TODO: a real-time clock can provide accurate timestamps later
    entry.creation_date = 0x0021;
    entry.last_access_date = 0x0021;
    entry.write_date = 0x0021;

    // a newly created empty file has cluster 0 and size 0
    entry.cluster_high = 0;
    entry.cluster_low = 0;
    entry.size = 0;

    u8 records[FAT32_CREATE_MAX_ENTRIES * 32];

    for (u32 i = 0; i < lfn_count; ++i) {
        memcpy(records + i * 32, &lfn[i], 32);
    }

    memcpy(records + lfn_count * 32, &entry, 32);

    u32 entry_lba;
    u16 entry_offset;

    b8 inserted = fat32_insert_directory_entries(fs, parent_cluster, records, lfn_count + 1, &entry_lba, &entry_offset);

    fat32_closedir(parent);

    if (!inserted) {
        return NULL;
    }

    if (fs->dev->flush && !fs->dev->flush(fs->dev->driver_data)) {
        return NULL;
    }

    FAT32_FILE* file = fat32_open(fs, path);

    if (!file) {
        return NULL;
    }

    return file;
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

// TODO: rollback
u32 fat32_write(FAT32_FILE* f, const void* buffer, u32 size)
{
    u32 original_first_cluster = f->first_cluster;

    if (!f || !f->fs || !buffer || size == 0) {
        return 0;
    }

    if (!f->fs->dev || !f->fs->dev->write || !f->cluster_buffer) {
        return 0;
    }

    u32 cluster_size = f->fs->sectors_per_cluster * f->fs->bytes_per_sector;

    if (cluster_size == 0) {
        return 0;
    }

    // do not support sparse files in the initial implementation
    if (f->pos > f->size) {
        return 0;
    }

    const u8* input = buffer;
    u32 total_written = 0;

    while (total_written < size) {
        u32 cluster_index = f->pos / cluster_size;
        u32 cluster_offset = f->pos % cluster_size;
        u32 cluster;

        if (!fat32_get_cluster_for_write(f, cluster_index, &cluster)) {
            break;
        }

        // preserve existing bytes in partial-cluster writes
        if (!fat32_read_cluster(f->fs, cluster, f->cluster_buffer)) {
            break;
        }

        u32 bytes_to_write = cluster_size - cluster_offset;
        if (bytes_to_write > size - total_written) {
            bytes_to_write = size - total_written;
        }

        memcpy(f->cluster_buffer + cluster_offset, input + total_written, bytes_to_write);

        if (!fat32_write_cluster(f->fs, cluster, f->cluster_buffer)) {
            break;
        }

        // the buffer contains the newly written cluster contents
        f->loaded_cluster = cluster;
        f->cluster_buffer_valid = true;

        f->pos += bytes_to_write;
        total_written += bytes_to_write;

        if (f->pos > f->size) {
            f->size = f->pos;
        }
    }

    if (total_written > 0 || f->first_cluster != original_first_cluster) {

        if (!fat32_update_file_entry(f)) {
            return 0;
        }

        if (f->fs->dev->flush && !f->fs->dev->flush(f->fs->dev->driver_data)) {
            return 0;
        }
    }

    return total_written;
}

int fat32_find_in_dir(FAT32_FS* fs, u32 start_cluster, const char* name, FAT32_DIRECTORY_ENTRY* out, u32* out_lba, u16* out_offset) {
    u32 cluster = start_cluster;
    if (cluster < 2 || cluster >= fs->cluster_count + 2) {
        return false;
    }
    u32 cluster_size = fs->sectors_per_cluster * 512;

    u8* buf = kmalloc(cluster_size);
    if (!buf) {
        return 0;
    }

    memset(buf, 0, cluster_size);

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

                if (out_lba) {
                    *out_lba = fat32_cluster_to_lba(fs, cluster) + (i / fs->bytes_per_sector);
                }

                if (out_offset) {
                    *out_offset = i % fs->bytes_per_sector;
                }

                kfree(buf);
                return 1;
            }

            // reset for next entry
            lfn_count = 0;
            memset(lfn_parts, 0, sizeof(lfn_parts));
        }

        if (!fat32_read_fat_entry(fs, cluster, &cluster)) {
            kfree(buf);
            return 0;
        }
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

int fat32_truncate(FAT32_FILE* f, u32 size) {
    if (!f || !f->fs || !f->fs->dev || !f->fs->dev->write || !f->fs->dev->read) {
        return -1;
    }

    // initial implementation supports truncate-to-zero only
    if (size != 0) {
        return -1;
    }

    FAT32_FS* fs = f->fs;
    u32 old_first_cluster = f->first_cluster;

    // Validate the current chain's starting cluster.
    if (old_first_cluster != 0 && (old_first_cluster < 2 || old_first_cluster >= fs->cluster_count + 2)) {
        return -1;
    }

    u32 old_size = f->size;
    u32 old_pos = f->pos;

    // make the file appear empty in memory and on disk; do this before freeing the old clusters so that a failure during freeing cannot leave the directory pointing at freed clusters
    f->first_cluster = 0;
    f->size = 0;
    f->pos = 0;

    if (!fat32_update_file_entry(f)) {
        // the directory update failed; restore the in-memory fields
        f->first_cluster = old_first_cluster;
        f->size = old_size;
        f->pos = old_pos;
        return -1;
    }

    // if a flush operation is available, synchronize the metadata before freeing the old data chain.
    if (fs->dev->flush && !fs->dev->flush(fs->dev->driver_data)) {
        // leave the old chain allocated, the on-disk state is uncertain
        f->cluster_buffer_valid = false;
        f->cluster_cursor_valid = false;
        return -1;
    }

    // reset the per-file cluster cache
    f->current_cluster = 0;
    f->current_cluster_index = 0;
    f->cluster_cursor_valid = false;
    f->cluster_buffer_valid = false;

    // free the old cluster chain, read each link before clearing the current FAT entry
    u32 cluster = old_first_cluster;
    u32 freed = 0;

    while (cluster != 0) {
        if (freed >= fs->cluster_count || cluster < 2 || cluster >= fs->cluster_count + 2) {
            // TODO:
            // corrupt/cyclic chain, the file is empty, but clusters may leak
            return -1;
        }

        u32 next;

        if (!fat32_read_fat_entry(fs, cluster, &next)) {
            return -1;
        }

        if (fat32_is_eoc(next)) {
            next = 0;
        }
        else if (!fat32_is_data_cluster(next) || next >= fs->cluster_count + 2) {
            // don't follow an invalid FAT link
            return -1;
        }

        if (!fat32_write_fat_entry(fs, cluster, 0)) {
            return -1;
        }

        cluster = next;
        ++freed;
    }

    // flush freed FAT entries when supported
    if (fs->dev->flush && !fs->dev->flush(fs->dev->driver_data)) {
        return -1;
    }

    return 0;
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

        if (!fat32_find_in_dir( fs, current_cluster, component, &entry, NULL, NULL)) {
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

    memset(dir->buffer, 0, dir->cluster_size);

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