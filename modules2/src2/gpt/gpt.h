#ifndef GPT_H
#define GPT_H

#include <stdint.h>

/**
 * @struct GPTHeader
 * @brief GUID Partition Table header structure
 */
typedef struct gpt_header_t
{
    char signature[8];                // "EFI PART" signature
    uint32_t revision;                // Header revision
    uint32_t header_size;             // Header size in bytes
    uint32_t header_crc32;            // Header CRC32 checksum
    uint32_t reserved;                // Reserved field
    uint64_t current_lba;             // LBA of this header
    uint64_t backup_lba;              // LBA of backup header
    uint64_t first_usable_lba;        // First usable LBA for partitions
    uint64_t last_usable_lba;         // Last usable LBA for partitions
    uint8_t disk_guid[16];            // Disk GUID
    uint64_t partition_entries_lba;   // LBA of partition entries
    uint32_t num_partition_entries;   // Number of partition entries
    uint32_t size_of_partition_entry; // Size of each partition entry
    uint32_t partition_entries_crc32; // CRC32 of partition entries
    uint8_t reserved2[420];           // Padding to 512 bytes
} __attribute__((packed)) gpt_header_t;

/**
 * @struct GPTPartitionEntry
 * @brief GPT partition entry structure
 */
typedef struct gpt_partition_entry_t
{
    uint8_t type_guid[16];   // Partition type GUID
    uint8_t unique_guid[16]; // Unique partition GUID
    uint64_t first_lba;      // First LBA of partition
    uint64_t last_lba;       // Last LBA of partition
    uint64_t attributes;     // Partition attributes
    uint16_t name[36];       // Partition name in UTF-16LE
} __attribute__((packed)) gpt_partition_entry_t;



#endif
