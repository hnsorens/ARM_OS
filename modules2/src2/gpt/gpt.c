#include "gpt/gpt.h"
#include "gpt/gpt_impl.h"

#include <stdint.h>
#include "gpt.h"

#include "kmm/kmm_inc.h"
#include "blk_dev/blk_dev_inc.h"
#include "str/str_inc.h"
#include "serial_debug/serial_debug_inc.h"
#include "bus_controller/bus_controller_inc.h"
#include "pmm/pmm_inc.h"

void* (*read_sectors)(uint32_t lba, uint32_t sector_count);
void (*write_sectors)(uint32_t lba, uint32_t sector_count, void *data);

typedef struct gpt_device_t
{
    uint8_t* buffer;
    blk_device_t dev;
} gpt_device_t;

int parse_gpt_partitions(blk_device_t dev, gpt_header_t* header, gpt_partition_t** partitions)
{
  // Calculate partition table location and size
  uint64_t entries_lba = header->partition_entries_lba;
  uint32_t num_entries = header->num_partition_entries;
  uint32_t entry_size = header->size_of_partition_entry;

  DEBUG("number of partitions: %s", header);
  // Read partition entries from disk
  //uint8_t* entries = read_sectors(entries_lba, (num_entries * entry_size + 511) / 512); // Round up to next sector
  uint8_t *entries = (uint8_t *)pmm_alloc_phys(6);
  blk_dev_read_sectors(dev, entries_lba, entries, (num_entries * entry_size + 511) / 512);
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

  pmm_free_phys(entries, 6);

  return valid_partitions;
}

gpt_partition_t* parse_gpt_header(uint32_t sector, uint8_t *buffer, blk_device_t dev)
{
  DEBUG("PARSING GPT HEADER");
  // Read GPT header from disk
  gpt_header_t* header = (gpt_header_t*)buffer;
  blk_dev_read_sectors(dev, sector, buffer, 1);
  DEBUG("GPT HEADER %lx\n", header);
  
  if (header != 0)
  {
    gpt_partition_t* partitions = 0;
    int num_partitions = parse_gpt_partitions(dev, header, &partitions);
    DEBUG("Found %d partitions!", num_partitions);

    if (num_partitions > 0)
    {
      return partitions;
    }
  }
  return 0;
}

gpt_partition_t *create(blk_device_t dev)
{
  uint8_t *header_sector_buffer = kmm_ksalloc(9);
  gpt_partition_t *gpt_partition =
      parse_gpt_header(1, header_sector_buffer, dev);

  kmm_ksfree(9, header_sector_buffer);

  return gpt_partition;
}

override void gpt_init(gpt_ops* ops)
{
    ops->create = create;
}

override void gpt_start(core_ops *kvtable)
{
    //  void* blk_device_base = (void*)bus_controller_find_device(2);
    // bus_controller_init_device(blk_device_base);

    // blk_device_t dev = blk_dev_create(blk_device_base);

    // uint8_t *buffer = kmm_ksalloc(9);

    // gpt_partition_t* gpt_partition = parse_gpt_header(1, buffer, dev);
}

override void gpt_fetch(core_ops *ops)
{
  kmm_fetch(ops);
  blk_dev_fetch(ops);
  str_fetch(ops);
  serial_debug_fetch(ops);
  bus_controller_fetch(ops);
  pmm_fetch(ops);
  
}
