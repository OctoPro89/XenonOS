#include "gpt.h"
#include <arch/x86_64/io.h>
#include <xlibc/string.h>
#include <memory/heap.h>

// EFI System Partition GUID:
const static uint8_t ESP_GUID[16] = {
    0x28,0x73,0x2A,0xC1,
    0x1F,0xF8,
    0xD2,0x11,
    0xBA,0x4B,
    0x00,0xA0,0xC9,0x3E,0xC9,0x3B
};

int gpt_find_fat32(block_device* dev, uint64_t* out_lba) {
    void* sector = kmalloc(512);

    // Read GPT header
    if (!dev->read(dev->driver_data, 1, 1, sector)) {
        kfree(sector);
        return 0;
    }

    gpt_header* hdr = (gpt_header*)sector;

    if (memcmp(hdr->signature, "EFI PART", 8) != 0) {
        serial_write_str("No GPT\n");
        kfree(sector);
        return 0;
    }

    uint64_t entries_lba = hdr->partition_entry_lba;
    uint32_t entry_size  = hdr->size_of_partition_entry;
    uint32_t count       = hdr->num_partition_entries;

    // Read entries (assume they fit in a few sectors)
    for (uint32_t i = 0; i < count; i++) {
        uint64_t lba = entries_lba + (i * entry_size) / 512;
        uint32_t off = (i * entry_size) % 512;

        dev->read(dev->driver_data, lba, 1, sector);

        gpt_entry* ent = (gpt_entry*)(sector + off);

        if (memcmp(ent->type_guid, ESP_GUID, 16) == 0) {
            *out_lba = ent->first_lba;
            kfree(sector);
            return 1;
        }
    }

    kfree(sector);

    return 0;
}