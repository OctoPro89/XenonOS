#pragma once
#include <xlibc/xstdint.h>
#include <filesystem/block_device/block_device.h>

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem[8];

    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;

    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 Extended
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;

    uint8_t  reserved[12];

    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} FAT32_BPB;

typedef struct {
    block_device* dev;

    uint32_t start_lba;

    uint32_t fat_start_lba;
    uint32_t data_start_lba;

    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;

    uint32_t root_cluster;
} FAT32_FS;

typedef struct __attribute__((packed)) {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;

    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;

    uint32_t size;
} FAT32_DIRECTORY_ENTRY;

typedef struct {
    FAT32_FS* fs;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t pos;
} FAT32_FILE;

#include "fat32.h"

int fat32_init(FAT32_FS* fs, block_device* dev, uint32_t part_lba);
uint32_t fat32_cluster_to_lba(FAT32_FS* fs, uint32_t cluster);
uint32_t fat32_read_fat_entry(FAT32_FS* fs, uint32_t cluster);
void fat32_read_cluster(FAT32_FS* fs, uint32_t cluster, void* buffer);
int fat32_name_match_8_3(const char* input, uint8_t* name);
FAT32_FILE* fat32_open(FAT32_FS* fs, const char* name);
uint32_t fat32_read(FAT32_FILE* f, void* buffer, uint32_t size);