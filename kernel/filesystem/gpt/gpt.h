#pragma once

#include <xlibc/xstdint.h>
#include <filesystem/block_device/block_device.h>

typedef struct {
    char     signature[8];   // "EFI PART"
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;

    uint64_t current_lba;
    uint64_t backup_lba;

    uint64_t first_usable_lba;
    uint64_t last_usable_lba;

    uint8_t  disk_guid[16];

    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_crc32;
} gpt_header;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];

    uint64_t first_lba;
    uint64_t last_lba;

    uint64_t attributes;
    uint16_t name[36]; // UTF-16
} gpt_entry;

/**
 * @brief Searches for a GPT partition using `dev`
 */
int gpt_find_fat32(block_device* dev, uint64_t* out_lba);