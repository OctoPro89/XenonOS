/*
    FAT32 Disk Operations driver for XenonOS
    TODO: better caching
*/

#pragma once
#include <kernel.h>
#include <xlibc/xstdint.h>
#include <filesystem/block_device/block_device.h>
#include <filesystem/vfs/vfs.h>

#define MAX_LFN_PARTS 20

typedef struct __packed__ {
    u8  jmp[3];
    u8  oem[8];

    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sector_count;
    u8  fat_count;
    u16 root_entry_count;
    u16 total_sectors_16;
    u8  media;
    u16 fat_size_16;

    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;

    // FAT32 Extended
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot_sector;

    u8  reserved[12];

    u8  drive_number;
    u8  reserved1;
    u8  boot_signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];
} FAT32_BPB;

typedef struct {
    block_device* dev;

    u32 start_lba;

    u32 fat_start_lba;
    u32 data_start_lba;

    u32 sectors_per_cluster;
    u32 bytes_per_sector;

    u32 root_cluster;
    
    // caching
    u32 fat_cache_lba;
    u8 fat_cache[512];
    b8 fat_cache_valid;

    u32 fat_size_sectors;
    u32 cluster_count;
    u8 fat_count;
} FAT32_FS;

typedef struct __packed__ {
    u8  name[11];
    u8  attr;
    u8  nt_reserved;
    u8  creation_time_tenths;
    u16 creation_time;
    u16 creation_date;
    u16 last_access_date;

    u16 cluster_high;
    u16 write_time;
    u16 write_date;
    u16 cluster_low;

    u32 size;
} FAT32_DIRECTORY_ENTRY;

typedef struct __packed__ {
    u8 order;
    u16 name1[5];
    u8 attr;
    u8 type;
    u8 checksum;
    u16 name2[6];
    u16 first_cluster_low;
    u16 name3[2];
} FAT32_LFN_ENTRY;

typedef struct {
    FAT32_FS* fs;
    u32 first_cluster;
    u32 size;
    u32 pos;

    u32 current_cluster;
    u32 current_cluster_index;
    b8 cluster_cursor_valid;

    u8* cluster_buffer;

    u32 loaded_cluster;
    b8 cluster_buffer_valid;

    // location of the file's short directory entry on disk
    u32 dir_entry_lba;
    u16 dir_entry_offset;
} FAT32_FILE;

typedef struct {
    FAT32_FS* fs;
    u32 cluster;
    u32 offset;
    u32 cluster_size;
    u8* buffer;

    // for assembling LFNs
    char lfn_parts[20][14];
    u32 lfn_count;
    u8 lfn_checksum;

    int end; // 0 = active, 1 = EOF, -1 = error
} FAT32_DIR;

int fat32_init(FAT32_FS* fs, block_device* dev, u32 part_lba);
u32 fat32_cluster_to_lba(FAT32_FS* fs, u32 cluster);
b8 fat32_read_fat_entry(FAT32_FS* fs, u32 cluster, u32* out_cluster);
b8 fat32_read_cluster(FAT32_FS* fs, u32 cluster, void* buffer);
b8 fat32_write_cluster(FAT32_FS* fs, u32 cluster, const void* buffer);
u32 fat32_entry_cluster(FAT32_DIRECTORY_ENTRY* ent);
void fat32_format_8_3(u8* name, char* out);
int fat32_name_match_8_3(const char* input, u8* name);
FAT32_FILE* fat32_open(FAT32_FS* fs, const char* name);
void fat32_close(FAT32_FILE* f);

/**
 * @brief Create a new empty short-name file and return it open.
 * @note Existing files are not overwritten.
 */
FAT32_FILE* fat32_create(FAT32_FS* fs, const char* path);

u32 fat32_read(FAT32_FILE* f, void* buffer, u32 size);
u32 fat32_write(FAT32_FILE* f, const void* buffer, u32 size);
int fat32_find_in_dir(FAT32_FS* fs, u32 start_cluster, const char* name, FAT32_DIRECTORY_ENTRY* out, u32* out_lba, u16* out_offset);
void fat32_lfn_extract(FAT32_LFN_ENTRY* lfn, char* out);
u32 fat32_get_size(FAT32_FILE* file);
int fat32_seek(FAT32_FILE* f, u32 offset);
u32 fat32_tell(FAT32_FILE* f);

FAT32_DIR* fat32_opendir(FAT32_FS* fs, const char* path);

/**
 * @brief Reads a directory
 * @returns 1 for an entry, 0 for end-of-directory, and -1 for an error
 */
int fat32_readdir(FAT32_DIR* dir, VFS_DIRENT* out);
void fat32_closedir(FAT32_DIR* dir);