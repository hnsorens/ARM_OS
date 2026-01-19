#include "module.h"
#include <stdint.h>

#include "modules/kmm.h"
#include "modules/blk_dev.h"
#include "modules/str.h"

#include "gpt.h"

#define debug "GPT"

vtable(gpt_vtable_t);
start(init, gpt_fetch, gpt_init);

void* (*read_sectors)(uint32_t lba, uint32_t sector_count);
void  (*write_sectors)(uint32_t lba, uint32_t sector_count, void* data);

int parse_gpt_partitions(gpt_header_t* header, gpt_partition_t** partitions)
{
  // Calculate partition table location and size
  uint64_t entries_lba = header->partition_entries_lba;
  uint32_t num_entries = header->num_partition_entries;
  uint32_t entry_size = header->size_of_partition_entry;

  // Read partition entries from disk
  uint8_t* entries = read_sectors(entries_lba, (num_entries * entry_size + 511) / 512); // Round up to next sector
  // Allocate output partition array
  *partitions = (gpt_partition_t*)kmm_kmalloc(num_entries * sizeof(gpt_partition_t));
  int valid_partitions = 0;

  // Process each partition entry
  for (int i = 0; i < num_entries; ++i)
  {
      gpt_partition_entry_t* entry = (gpt_partition_entry_t*)(entries + i * entry_size);

      // Skip empty entries
      if (entry->type_guid[0] == 0)
      {
          continue;
      }

      // Populate partition structure
      gpt_partition_t* partition = &(*partitions)[valid_partitions];
      str_memcpy(partition->type_guid, entry->type_guid, 16);
      str_memcpy(partition->unique_guid, entry->unique_guid, 16);
      partition->first_lba = entry->first_lba;
      partition->last_lba = entry->last_lba;
      partition->attributes = entry->attributes;

      // Convert UTF-16 name
      for (int j = 0; j < 36 && entry->name[j] != 0; ++j)
      {
          partition->name[j] = (char)entry->name[j];
      }

      valid_partitions++;
  }

  return valid_partitions;
}

gpt_partition_t* parse_gpt_header(uint32_t lba)
{
  DEBUG("PARSING GPT HEADER");
  // Read GPT header from disk
  gpt_header_t* header = (gpt_header_t*)read_sectors(lba, 1);
  if (header != 0)
  {
    gpt_partition_t* partitions = 0;
    int num_partitions = parse_gpt_partitions(header, &partitions);
    DEBUG("Found %d partitions!", num_partitions);

    if (num_partitions > 0)
    {
      return partitions;
    }
  }
  return 0;
}

void init(gpt_vtable_t* vtable)
{
  
}

void gpt_init(kernel_vtable_t *kvtable)
{
  DEBUG("INIT");
}

void gpt_fetch(kernel_vtable_t *kvtable)
{
  kmm_fetch(kvtable);
  blk_dev_fetch(kvtable);
  str_fetch(kvtable);
}