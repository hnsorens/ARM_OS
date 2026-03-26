#ifndef GPT_TYPES_H
#define GPT_TYPES_H

#include "stdint.h"

typedef struct gpt_partition_t
{
    uint8_t type_guid[16];   ///< Partition type GUID
    uint8_t unique_guid[16]; ///< Unique partition GUID
    uint64_t first_lba;      ///< First LBA of partition
    uint64_t last_lba;       ///< Last LBA of partition
    uint64_t attributes;     ///< Partition attributes
    uint16_t name[36];       ///< Partition name in UTF-16LE
} gpt_partition_t;

typedef void* blk_device_t;

#endif