#include "module.h"
#include <stdint.h>

#include "modules/vtables/blk_dev.h"

#include "modules/serial_debug.h"
#include "modules/bus_controller.h"
#include "modules/kmm.h"
#include "modules/str.h"
#include "modules/pmm.h"

#define debug "VIRTIO_BLK"

vtable(blk_dev_vtable_t)
start(init, virtio_blk_fetch, virtio_blk_init)

#define VIRTIO_BLK_DEVICE_ID 2

// Feature bits (from spec)
#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)
#define VIRTIO_BLK_F_SEG_MAX        (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY       (1 << 4)
#define VIRTIO_BLK_F_RO             (1 << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1 << 6)
#define VIRTIO_BLK_F_TOPOLOGY       (1 << 10)
#define VIRTIO_BLK_F_FLUSH          (1 << 9)

// Request types (LE format)
#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_T_FLUSH_OUT      5

// Status bytes
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

// VirtIO MMIO v1.0+ (Modern) Registers
#define VIRTIO_MMIO_MAGIC_VALUE        0x000
#define VIRTIO_MMIO_VERSION            0x004
#define VIRTIO_MMIO_DEVICE_ID          0x008
#define VIRTIO_MMIO_VENDOR_ID          0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES    0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES    0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL          0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX      0x034
#define VIRTIO_MMIO_QUEUE_NUM          0x038
#define VIRTIO_MMIO_QUEUE_ALIGN        0x03c
#define VIRTIO_MMIO_QUEUE_PFN          0x040  // Legacy v1 only
#define VIRTIO_MMIO_QUEUE_READY        0x044  // Modern: Queue ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY       0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS   0x060
#define VIRTIO_MMIO_INTERRUPT_ACK      0x064
#define VIRTIO_MMIO_STATUS             0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW     0x080  // Modern: 64-bit queue address
#define VIRTIO_MMIO_QUEUE_DESC_HIGH    0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW   0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH  0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH  0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION  0x0fc
#define VIRTIO_MMIO_CONFIG             0x100

// Modern feature bits
#define VIRTIO_F_VERSION_1             (1UL << 32)  // Bit 32: Modern device
#define VIRTIO_F_RING_INDIRECT_DESC    (1UL << 28)
#define VIRTIO_F_RING_EVENT_IDX        (1UL << 29)
#define VIRTIO_F_IN_ORDER              (1UL << 35)

// Status bits
#define VIRTIO_STATUS_ACKNOWLEDGE      0x01
#define VIRTIO_STATUS_DRIVER           0x02
#define VIRTIO_STATUS_FEATURES_OK      0x08
#define VIRTIO_STATUS_DRIVER_OK        0x04
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED           0x80

// Status bits
#define VIRTIO_STATUS_ACKNOWLEDGE      0x01
#define VIRTIO_STATUS_DRIVER           0x02
#define VIRTIO_STATUS_FEATURES_OK      0x08
#define VIRTIO_STATUS_DRIVER_OK        0x04
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED           0x80

#define PAGE_SHIFT 12  // 4KB pages

#define offsetof(type, member) ((unsigned long)(&((type *)0)->member))

// Configuration structure (little-endian)
typedef struct virtio_blk_config_t {
    uint64_t capacity;          // Device capacity in 512-byte sectors
    uint32_t size_max;          // Max segment size
    uint32_t seg_max;           // Max segments per request
    
    struct {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;
    
    uint32_t blk_size;          // Block size
    
    struct {
        uint8_t physical_block_exp;
        uint8_t alignment_offset;
        uint16_t min_io_size;
        uint32_t opt_io_size;
    } topology;
    
    uint8_t writeback;
} __attribute__((packed)) virtio_blk_config_t;

// Request header (little-endian)
typedef struct virtio_blk_req_t {
    uint32_t type;              // VIRTIO_BLK_T_IN, VIRTIO_BLK_T_OUT, etc.
    uint32_t reserved;          // Was ioprio in legacy
    uint64_t sector;            // Sector number (0 for flush)
} __attribute__((packed)) virtio_blk_req_t;

// Virtqueue structures (16 bytes per descriptor)
typedef struct virtq_desc_t {
    uint64_t addr;      // Physical address
    uint32_t len;       // Length
    uint16_t flags;     // Flags
    uint16_t next;      // Next descriptor if flags & VIRTQ_DESC_F_NEXT
} virtq_desc_t;

typedef struct virtq_avail_t {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];    // Size = queue_size
} virtq_avail_t;

typedef struct virtq_used_elem_t {
    uint32_t id;        // Index of start of descriptor chain
    uint32_t len;       // Total length written
} virtq_used_elem_t;

typedef struct virtq_used_t {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];  // Size = queue_size
} virtq_used_t;

// Device structure
typedef struct virtio_blk_device_t {
    void *mmio_base;
    virtio_blk_config_t *config;
    
    // Virtqueue structures
    struct {
        uint16_t size;
        uint16_t free_head;
        uint16_t last_used_idx;
        
        virtq_desc_t *desc;
        virtq_avail_t *avail;
        virtq_used_t *used;
        
        void *queue_mem;  // Base of contiguous queue memory (v1 requirement)
    } queue;
    
    uint32_t block_size;
    uint64_t capacity;
    uint32_t features;
} virtio_blk_device_t;

// Descriptor flags
#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2
#define VIRTQ_DESC_F_INDIRECT   4

// Memory barriers for ARM
static inline void dmb(void) {
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline uint32_t mmio_read32(void *base, uint32_t offset) {
    volatile uint32_t *addr = (volatile uint32_t *)((uintptr_t)base + offset);
    dmb();
    uint32_t val = *addr;
    dmb();
    return val;
}

static inline void mmio_write32(void *base, uint32_t offset, uint32_t value) {
    volatile uint32_t *addr = (volatile uint32_t *)((uintptr_t)base + offset);
    dmb();
    *addr = value;
    dmb();
}

static inline uintptr_t virt_to_phys(void *va) {
    return (uintptr_t)va;
}

static inline uint32_t le32_to_cpu(uint32_t val) {
    return val;
}

static inline uint64_t le64_to_cpu(uint64_t val) {
    return val;
}

void* alloc(unsigned long size)
{
    return pmm_alloc_phys(0);
}

// Public API
void* virtio_blk_create(void *mmio_base)
{
    uint32_t version = mmio_read32(mmio_base, VIRTIO_MMIO_VERSION);
    if (version != 2) {
        ERROR("Only VirtIO 1.0+ (version 2) supported. Found version %u", version);
        return 0;
    }

    // Check VERSION_1 feature bit
    mmio_write32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1); // Select high word
    uint32_t features_high = mmio_read32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES);
    if (!(features_high & 1)) { // Bit 0 of high word = bit 32 overall
        ERROR("Device lacks VERSION_1 feature (not a valid VirtIO 1.0+ device)");
        return 0;
    }

    virtio_blk_device_t* dev = (virtio_blk_device_t*)alloc(sizeof(virtio_blk_device_t));
    DEBUG("Allocated Device at %x", dev);

    str_memset(dev, 0, sizeof(virtio_blk_device_t));
    DEBUG("Cleared Device");
    dev->mmio_base = mmio_base;

    // Config space is at offset 0x100
    dev->config = (virtio_blk_config_t*)((uintptr_t)mmio_base + VIRTIO_MMIO_CONFIG);
    DEBUG("Set config");

    // Read block size
    dev->block_size = le32_to_cpu(dev->config->blk_size);
    if (dev->block_size == 0) {
        dev->block_size = 512; // Default
    }
    DEBUG("Block size = %u", dev->block_size);

    dev->capacity = le64_to_cpu(dev->config->capacity);
    if (dev->capacity == 0) {
        DEBUG("WARNING: device capacity = 0, setting fallback 1M sectors");
        dev->capacity = 1024 * 1024;
    }
    DEBUG("Device capacity = %llu sectors", dev->capacity);

    dev->queue.size = 8;

    if (bus_controller_setup_queue(dev->mmio_base, 0, &dev->queue) != 0)
    {
        ERROR("Failed to setup queue");
        return 0;
    }

    // Set DRIVER_OK status
    DEBUG("Setting DRIVER_OK");
    uint32_t status = mmio_read32(mmio_base, VIRTIO_MMIO_STATUS);
    DEBUG("Current status: 0x%x", status);
    
    status |= 4;  // DRIVER_OK
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, status);
    
    status = mmio_read32(mmio_base, VIRTIO_MMIO_STATUS);
    DEBUG("Status after DRIVER_OK: 0x%x", status);

    return dev;
}

// Read a sector
int virtio_blk_read(void *dev, uint64_t sector, void *buffer) {
    virtio_blk_device_t *virt_dev = (virtio_blk_device_t*)dev;

    if (!dev) {
        ERROR("virtio_blk_read: device pointer is NULL");
        return -1;
    }

    DEBUG("virtio_blk_read: sector=%llu, buffer=%x", sector, buffer);

    if (sector >= virt_dev->capacity) {
        ERROR("virtio_blk_read: sector %llu >= capacity %llu",
              sector, virt_dev->capacity);
        return -1;
    }

    str_memset(buffer, 0xAA, virt_dev->block_size);
    DEBUG("Buffer cleared with pattern 0xAA");

    // Allocate DMA-safe request + status
    virtio_blk_req_t *req = alloc(sizeof(*req));
    DEBUG("ALLOC REQ %lx\n", req);
    uint8_t *status = alloc(1);
    DEBUG("ALLOC STATUS %lx\n", status);

    if (!req || !status) {
        ERROR("Failed to allocate request/status");
        return -1;
    }

    req->type     = VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector   = sector;
    *status       = 0xFF;

    int ret = bus_controller_submit_request(virt_dev->mmio_base, VIRTIO_BLK_T_IN,
                &virt_dev->queue,
                req, sizeof(*req),
                (void*)buffer, virt_dev->block_size,
                status);
    
    if (ret != 0) {
        ERROR("virtio_blk_read: submit_request failed, ret=%d", ret);
    } else {
        DEBUG("virtio_blk_read: success, first 16 bytes:");
        for (int i = 0; i < 16 && i < virt_dev->block_size; i++) {
            DEBUG("  buffer[%d] = 0x%02x", i, ((uint8_t*)buffer)[i]);
        }
    }

    mmio_write32(virt_dev->mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    DEBUG("Signaled used buffer notification to device");

    

    return ret;
}

// Write a sector
int virtio_blk_write(void *dev, uint64_t sector, const void *buffer) {
    virtio_blk_device_t *virt_dev = (virtio_blk_device_t*)dev;

    if (sector >= virt_dev->capacity) {
        return -1;
    }
    
    if (virt_dev->features & VIRTIO_BLK_F_RO) {
        return -1;
    }

    // Allocate DMA-safe request + status
    virtio_blk_req_t *req = alloc(sizeof(*req));
    DEBUG("ALLOC REQ %lx\n", req);
    uint8_t *status = alloc(1);
    DEBUG("ALLOC STATUS %lx\n", status);

    if (!req || !status) {
        ERROR("Failed to allocate request/status");
        return -1;
    }

    req->type     = VIRTIO_BLK_T_OUT;
    req->reserved = 0;
    req->sector   = sector;
    *status       = 0xFF;

    return bus_controller_submit_request(virt_dev->mmio_base, VIRTIO_BLK_T_OUT,
                &virt_dev->queue,
                req, sizeof(*req),
                (void*)buffer, virt_dev->block_size,
                status);
}

// Flush device cache
int virtio_blk_flush(void *dev) {
    virtio_blk_device_t *virt_dev = (virtio_blk_device_t*)dev;

    if (!(virt_dev->features & VIRTIO_BLK_F_FLUSH)) {
        return 0;
    }

    return bus_controller_submit_request(virt_dev->mmio_base, VIRTIO_BLK_T_FLUSH,
                &virt_dev->queue,
                0, 0,
                0, 0,
                0);
}

void virtio_blk_fetch(kernel_vtable_t *kvtable)
{
    bus_controller_fetch(kvtable);
    kmm_fetch(kvtable);
    str_fetch(kvtable);
    pmm_fetch(kvtable);
    serial_debug_fetch(kvtable);
}

void init(blk_dev_vtable_t *vtable)
{
    vtable->create = virtio_blk_create;
    vtable->read_sectors = virtio_blk_read;
    vtable->write_sector = virtio_blk_write;
    vtable->flush = virtio_blk_flush;
}

void virtio_blk_init(kernel_vtable_t* kvtable)
{
    DEBUG("Init");
    
    void* blk_device_base = (void*)bus_controller_find_device(VIRTIO_BLK_DEVICE_ID);
    bus_controller_init_device((void*)blk_device_base);

    DEBUG("Device Base: %x", blk_device_base);

    virtio_blk_device_t* dev = (virtio_blk_device_t*)virtio_blk_create(blk_device_base);

    // Test reading sectors
    uint8_t* buffer = alloc(10);
    int ret = virtio_blk_read(dev, 2, buffer);
    if (ret != 0) {
        ERROR("[GPT TEST] Failed to read sector 0, ret=%d", ret);
        return;
    }

    ret = virtio_blk_read(dev, 1, buffer);
    if (ret != 0) {
        ERROR("[GPT TEST] Failed to read sector 1 (GPT header), ret=%d", ret);
        return;
    }

    // Check GPT signature
    char sig[9] = {0};
    for (int i = 0; i < 8; i++) {
        sig[i] = buffer[i];
    }

    DEBUG("[GPT TEST] GPT signature: %c%c%c%c%c%c%c%c",
          sig[0], sig[1], sig[2], sig[3],
          sig[4], sig[5], sig[6], sig[7]);
}