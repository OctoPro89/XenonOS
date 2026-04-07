#include "fat32.h"
#include <arch/x86_64/io.h>
#include <xlibc/string.h>
#include <xlibc/ctype.h>
#include <xlibc/stdlib.h>

int fat32_init(FAT32_FS* fs, block_device* dev, uint32_t part_lba) {
    uint8_t sector[512];

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

    uint32_t fat_size = bpb->fat_size_32;

    fs->fat_start_lba = part_lba + bpb->reserved_sector_count;

    fs->data_start_lba = fs->fat_start_lba + (bpb->fat_count * fat_size);

    serial_write_str("FAT32 OK\n");
    return 1;
}

uint32_t fat32_cluster_to_lba(FAT32_FS* fs, uint32_t cluster) {
    return fs->data_start_lba + (cluster - 2) * fs->sectors_per_cluster;
}

uint32_t fat32_read_fat_entry(FAT32_FS* fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;

    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t sector[512];
    fs->dev->read(fs->dev->driver_data, fat_sector, 1, sector);

    uint32_t value = *(uint32_t*)(sector + ent_offset);

    return value & 0x0FFFFFFF;
}

void fat32_read_cluster(FAT32_FS* fs, uint32_t cluster, void* buffer) {
    uint32_t lba = fat32_cluster_to_lba(fs, cluster);
    fs->dev->read(fs->dev->driver_data, lba, fs->sectors_per_cluster, buffer);
}

int fat32_name_match_8_3(const char* input, uint8_t* name) {
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

FAT32_FILE* fat32_open(FAT32_FS* fs, const char* name) {
    uint32_t cluster = fs->root_cluster;

    uint32_t cluster_size = fs->sectors_per_cluster * 512;
    uint8_t* buf = malloc(cluster_size);

    while (cluster < 0x0FFFFFF8) {
        fat32_read_cluster(fs, cluster, buf);

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            FAT32_DIRECTORY_ENTRY* ent = (FAT32_DIRECTORY_ENTRY*)(buf + i);

            if (ent->name[0] == 0x00) { return NULL; }
            if (ent->name[0] == 0xE5) { continue; }
            if (ent->attr == 0x0F) { continue; }

            if (fat32_name_match_8_3(name, ent->name)) {
                FAT32_FILE* f = malloc(sizeof(FAT32_FILE));

                f->fs = fs;
                f->first_cluster = (ent->cluster_high << 16) | ent->cluster_low;
                f->size = ent->size;
                f->pos = 0;

                return f;
            }
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    return NULL;
}

uint32_t fat32_read(FAT32_FILE* f, void* buffer, uint32_t size) {
    uint8_t* out = buffer;
    uint32_t read = 0;

    uint32_t cluster = f->first_cluster;
    uint32_t cluster_size = f->fs->sectors_per_cluster * 512;

    uint8_t* temp = malloc(cluster_size);

    while (cluster < 0x0FFFFFF8 && read < size) {
        fat32_read_cluster(f->fs, cluster, temp);

        uint32_t to_copy = cluster_size;
        if (read + to_copy > size) {
            to_copy = size - read;
        }
        
        memcpy(out + read, temp, to_copy);
        read += to_copy;

        cluster = fat32_read_fat_entry(f->fs, cluster);
    }

    return read;
}