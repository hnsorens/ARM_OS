#include "module.h"
#include <stdbool.h>
#include <stdint.h>

#include "modules/bus_controller.h"
#include "modules/vtables/bus_controller.h"

#include "modules/pmm.h"
#include "modules/str.h"
#include "modules/serial_debug.h"

#define debug "VIRTIO"

vtable(bus_controller_vtable_t)
start(init, virtio_fetch, virtio_init)

#define VIRTIO_MAGIC 0x74726976

// Request types (LE format)
#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_T_FLUSH_OUT      5

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

// Descriptor flags
#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2
#define VIRTQ_DESC_F_INDIRECT   4

#define offsetof(type, member) ((unsigned long)(&((type *)0)->member))

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

typedef struct virtio_queue_t {
    uint16_t size;
    uint16_t free_head;
    uint16_t last_used_idx;
    
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    
    void *queue_mem;  // Base of contiguous queue memory (v1 requirement)
} virtio_queue_t;

static inline uintptr_t virt_to_phys(void *va) {
    return (uintptr_t)va;
}

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

static inline uint32_t mmio_read64(void *base, uint32_t offset) {
    volatile uint32_t *addr = (volatile uint32_t *)((uintptr_t)base + offset);
    dmb();
    uint32_t val = *addr;
    dmb();
    return val;
}

static inline void mmio_write64(void *base, uint32_t offset, uint32_t value) {
    volatile uint32_t *addr = (volatile uint32_t *)((uintptr_t)base + offset);
    dmb();
    *addr = value;
    dmb();
}

// Find VirtIO block device with scanning
static uintptr_t find_virtio_device(uint8_t id) {
    DEBUG("Starting VirtIO device scan...");
    
    // Scan common VirtIO MMIO addresses
    for (uintptr_t base = 0x0A000000; base < 0x0A004000; base += 0x200) {
        DEBUG("Checking address 0x%08lx", base);
        
        uint32_t magic = mmio_read32((void*)base, 0);
        
        if (magic == VIRTIO_MAGIC) {
            DEBUG("Found VirtIO device at 0x%08lx", base);
            
            uint32_t version = mmio_read32((void*)base, 0x004);
            uint32_t device_id = mmio_read32((void*)base, 0x008);
            uint32_t vendor_id = mmio_read32((void*)base, 0x00C);
            
            DEBUG("Version: %d, Device ID: 0x%x, Vendor: 0x%04x", 
                  version, device_id, vendor_id & 0xFFFF);
            
            if (device_id == id) {
                DEBUG("*** DEVICE FOUND at 0x%08lx ***", base);
                return base;
            }
        }
    }
    
    ERROR("No VirtIO block device found!");
    DEBUG("Scanned addresses: 0x0A000000 - 0x0A00F000");
    return 0;
}

void* alloc(unsigned long size)
{
    return pmm_alloc_phys(0);
}

static int virtio_mmio_common_init(void* base) {
    DEBUG("=== VirtIO MMIO common init @ 0x%08lx ===", base);

    /* ------------------------------------------------------------
     * 0. Identify device
     * ------------------------------------------------------------ */
    uint32_t magic   = mmio_read32(base, 0x000);
    uint32_t version = mmio_read32(base, 0x004);
    uint32_t device  = mmio_read32(base, 0x008);

    if (magic != 0x74726976) {
        ERROR("Invalid VirtIO magic");
        return -1;
    }

    if (version != 2) {
        ERROR("Unsupported VirtIO version %u", version);
        return -1;
    }

    if (device == 0) {
        ERROR("No VirtIO device present");
        return -1;
    }

    DEBUG("Device ID %u, version %u", device, version);

    // Reset device
    mmio_write32(base, 0x070, 0x00);

    // ACKNOWLEDGE + DRIVER
    uint32_t status = 0;

    status |= 0x01; /* ACKNOWLEDGE */
    mmio_write32(base, 0x070, status);

    status |= 0x02; /* DRIVER */
    mmio_write32(base, 0x070, status);

    // Discover device features
    uint32_t dev_features[2] = {0, 0};

    for (uint32_t i = 0; i < 2; i++) {
        mmio_write32(base, 0x014, i);
        dev_features[i] = mmio_read32(base, 0x010);
        DEBUG("Device features[%u] = 0x%08x", i, dev_features[i]);
    }

    DEBUG("Status: 0x%x", mmio_read32(base, 0x070));

    // Select common, device-independent features
    uint32_t drv_features[2] = {0, 0};

    /*
     * The ONLY universally valid feature is VERSION_1.
     * Everything else is device-specific by definition.
     */
    const uint32_t VIRTIO_F_VERSION_1 = 32;

    if (dev_features[1] & (1u << (VIRTIO_F_VERSION_1 - 32))) {
        drv_features[1] |= (1u << (VIRTIO_F_VERSION_1 - 32));
        DEBUG("Negotiating VERSION_1 (modern VirtIO)");
    } else {
        DEBUG("Legacy VirtIO device (no VERSION_1)");
    }

    // Write negotiated features
    for (uint32_t i = 0; i < 2; i++) {
        mmio_write32(base, 0x024, i);
        mmio_write32(base, 0x020, drv_features[i]);
    }

    // FEATURES_OK handshake
    status |= 0x08; /* FEATURES_OK */
    mmio_write32(base, 0x070, status);

    if (dev_features[0] & (1u << 29)) { // VIRTIO_F_RING_EVENT_IDX
        drv_features[0] |= (1u << 29);
        DEBUG("Negotiating RING_EVENT_IDX for better performance");
    }

    if (dev_features[0] & (1u << 29)) { // VIRTIO_F_RING_EVENT_IDX
        drv_features[0] |= (1u << 29);
        DEBUG("Negotiating RING_EVENT_IDX for better performance");
    }

    for (uint32_t i = 0; i < 2; i++) {
        mmio_write32(base, 0x024, i);
        mmio_write32(base, 0x020, drv_features[i]);
    }

    status |= 0x08; /* FEATURES_OK */
    mmio_write32(base, 0x070, status);

    status = mmio_read32(base, 0x070);
    if (!(status & 0x08)) {
        ERROR("Device rejected negotiated features");
        return -1;
    }

    DEBUG("Feature negotiation successful for modern VirtIO");

    /*
     * DO NOT:
     *  - touch queues
     *  - set DRIVER_OK
     *  - access device-specific config space
     */

    return 0;
}

int virtio_setup_queue(void* base, uint32_t queue_idx, void* queue)
{
    virtio_queue_t* virtqueue = queue;
    DEBUG("Settup up queue %u\n", queue_idx);

    // Select Queue
    mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    // Check max queue size
    uint32_t max_size = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_size == 0)
    {
        ERROR("Queue %u not available", queue_idx);
        return -1;
    }

    DEBUG("Queue %u max size: %u", queue_idx, max_size);

    // Use queue size or max size
    uint16_t queue_size = virtqueue->size;
    if (queue_size == 0 || queue_size > max_size)
    {
        queue_size = max_size;
        virtqueue->size = queue_size;
    }

    // Write queue size
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    unsigned long desc_size = queue_size * sizeof(virtq_desc_t);
    unsigned long avail_size = offsetof(virtq_avail_t, ring) + queue_size * sizeof(uint16_t);
    unsigned long used_size = offsetof(virtq_used_t, ring) + queue_size * sizeof(virtq_used_elem_t);

    virtqueue->desc = (virtq_desc_t *)alloc(desc_size);
    virtqueue->avail = (virtq_avail_t *)alloc(avail_size);
    virtqueue->used = (virtq_used_t *)alloc(used_size);

    if (!virtqueue->desc || !virtqueue->avail || !virtqueue->used)
    {
        ERROR("Failed to allocate queue memeory");
        return -1;
    }

    str_memset(virtqueue->desc, 0, desc_size);
    str_memset(virtqueue->avail, 0, avail_size);
    str_memset(virtqueue->used, 0, used_size);

    // Initialize free descriptor list
    for (uint16_t i = 0; i < queue_size; i++)
    {
        virtqueue->desc[i].next = i + 1;
    }
    virtqueue->desc[queue_size - 1].next = 0xFFFF;
    virtqueue->free_head = 0;
    
    virtqueue->avail->idx = 0;
    virtqueue->last_used_idx = 0;
    
    // Write physical addresses to device (64-bit split into low/high)
    uint64_t desc_pa = virt_to_phys(virtqueue->desc);
    uint64_t avail_pa = virt_to_phys(virtqueue->avail);
    uint64_t used_pa = virt_to_phys(virtqueue->used);
    
    DEBUG("Queue physical addresses:");
    DEBUG("  desc:  0x%llx", desc_pa);
    DEBUG("  avail: 0x%llx", avail_pa);
    DEBUG("  used:  0x%llx", used_pa);
    
    // Write descriptor table address
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW, 
                 (uint32_t)(desc_pa & 0xFFFFFFFF));
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, 
                 (uint32_t)(desc_pa >> 32));
    
    // Write available ring address
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, 
                 (uint32_t)(avail_pa & 0xFFFFFFFF));
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 
                 (uint32_t)(avail_pa >> 32));
    
    // Write used ring address
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, 
                 (uint32_t)(used_pa & 0xFFFFFFFF));
    mmio_write32(base, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 
                 (uint32_t)(used_pa >> 32));
    
    // Mark queue as ready
    mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
    
    // Verify queue is ready
    uint32_t ready = mmio_read32(base, VIRTIO_MMIO_QUEUE_READY);
    if (!(ready & 1)) {
        ERROR("Queue %u not ready after setup", queue_idx);
        return -1;
    }
    
    DEBUG("Queue %u setup complete", queue_idx);
    return 0;
}


int virtio_submit_request(void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status)
{
    DEBUG("Sending request");

    virtio_queue_t* virtqueue = queue;

    // Allocate descriptors: header + data + status
    uint16_t descs[3];

    for (int i = 0; i < 3; i++) {
        if (virtqueue->free_head == 0xFFFF) {
            ERROR("No free descriptors");
            return -1;
        }

        descs[i] = virtqueue->free_head;
        virtqueue->free_head = virtqueue->desc[descs[i]].next;

        DEBUG("Allocated desc[%d] = %u", i, descs[i]);
    }

    // Descriptor 0: request header
    virtqueue->desc[descs[0]].addr  = virt_to_phys(req);
    virtqueue->desc[descs[0]].len   = req_len;
    virtqueue->desc[descs[0]].flags = VIRTQ_DESC_F_NEXT;
    virtqueue->desc[descs[0]].next  = descs[1];

    // Descriptor 1: data buffer
    virtqueue->desc[descs[1]].addr = virt_to_phys(data);
    virtqueue->desc[descs[1]].len  = data_len;

    if (type == VIRTIO_BLK_T_IN) {
        virtqueue->desc[descs[1]].flags =
            VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
        DEBUG("Data direction: device → memory (READ)");
    } else {
        virtqueue->desc[descs[1]].flags = VIRTQ_DESC_F_NEXT;
        DEBUG("Data direction: memory → device (WRITE)");
    }

    virtqueue->desc[descs[1]].next = descs[2];

    // Descriptor 2: status byte
    virtqueue->desc[descs[2]].addr  = virt_to_phys(status);
    virtqueue->desc[descs[2]].len   = 1;
    virtqueue->desc[descs[2]].flags = VIRTQ_DESC_F_WRITE;
    virtqueue->desc[descs[2]].next  = 0;

    DEBUG("Descriptor chain built: %u -> %u -> %u",
          descs[0], descs[1], descs[2]);

    // Add to available ring
    uint16_t avail_idx = virtqueue->avail->idx % virtqueue->size;
    virtqueue->avail->ring[avail_idx] = descs[0];

    DEBUG("Added to avail ring idx=%u desc=%u",
          avail_idx, descs[0]);

    dmb();
    virtqueue->avail->idx++;
    dmb();

    // Notify device
    DEBUG("Notifying device (queue 0)");
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    // Poll for completion
    uint16_t old_used = virtqueue->last_used_idx;
    DEBUG("Waiting for completion (used.idx=%u)", old_used);

    volatile virtq_used_t *used = virtqueue->used;
    volatile uint8_t *status_ptr = status;

    while (used->idx == old_used) {
        dmb();
        DEBUG("=== ACTUAL DESCRIPTOR MEMORY DUMP ===");
        virtq_desc_t *d0 = &virtqueue->desc[0];
        virtq_desc_t *d1 = &virtqueue->desc[1];
        virtq_desc_t *d2 = &virtqueue->desc[2];

        DEBUG("Desc[0] at %p: addr=0x%llx len=%u flags=0x%x next=%u",
            d0, d0->addr, d0->len, d0->flags, d0->next);
        DEBUG("Desc[1] at %p: addr=0x%llx len=%u flags=0x%x next=%u",
            d1, d1->addr, d1->len, d1->flags, d1->next);
        DEBUG("Desc[2] at %p: addr=0x%llx len=%u flags=0x%x next=%u",
            d2, d2->addr, d2->len, d2->flags, d2->next);

        DEBUG("Avail ring at %p: flags=%u idx=%u ring[0]=%u",
            virtqueue->avail, virtqueue->avail->flags, 
            virtqueue->avail->idx, virtqueue->avail->ring[0]);

        DEBUG("Used ring at %p: flags=%u idx=%u",
            virtqueue->used, virtqueue->used->flags, virtqueue->used->idx);

        DEBUG("Desc offset from base: 0x%lx", (uint8_t*)virtqueue->desc - (uint8_t*)virtqueue->queue_mem);
        DEBUG("Avail offset from base: 0x%lx", (uint8_t*)virtqueue->avail - (uint8_t*)virtqueue->queue_mem);
        DEBUG("Used offset from base: 0x%lx", (uint8_t*)virtqueue->used - (uint8_t*)virtqueue->queue_mem);
    }

    DEBUG("Request completed! used.idx is now %u", used->idx);

    uint16_t used_idx = old_used % virtqueue->size;
    virtq_used_elem_t *used_elem = &used->ring[used_idx];

    // Free the descriptor chain
    uint16_t cur = used_elem->id;
    while (1) {
        uint16_t next = virtqueue->desc[cur].next;
        virtqueue->desc[cur].next = virtqueue->free_head;
        virtqueue->free_head = cur;

        DEBUG("Freed desc %u", cur);

        if (!(virtqueue->desc[cur].flags & VIRTQ_DESC_F_NEXT))
            break;

        cur = next;
    }

    virtqueue->last_used_idx++;

    DEBUG("Request completed, status=%u", *status_ptr);

    return 0;
}

uintptr_t virtio_find_device(uint8_t id)
{
    DEBUG("FINDING DEVICE\n");
    uintptr_t device_base = find_virtio_device(id);
    
    if (device_base == 0) {
        ERROR("No block device found - stopping");
        DEBUG("Make sure QEMU command has: -device virtio-blk-device,drive=disk0");
        return 0;
    }

    DEBUG("Found Device with ID: %d at %08x\n", id, device_base);
    return device_base;
}

void virtio_init_device(void* device_base)
{
    DEBUG("INITIALIZING DEVICE\n");
    int init_result = virtio_mmio_common_init(device_base);
    
    if (init_result != 0) {
        ERROR("Device initialization failed with code %d", init_result);
        DEBUG("Stopping due to initialization failure");
        return;
    }
    
    DEBUG("INITIALIZED DEVICE");
}

void virtio_fetch(kernel_vtable_t* kvtable) 
{
    pmm_fetch(kvtable);
    str_fetch(kvtable);
    bus_controller_fetch(kvtable);
    serial_debug_fetch(kvtable);
}

void virtio_init(kernel_vtable_t* kvtable) 
{
    DEBUG("INIT");
}

void init(bus_controller_vtable_t* vtable) 
{
    vtable->init_device = virtio_init_device;
    vtable->find_device = virtio_find_device;
    vtable->setup_queue = virtio_setup_queue;
    vtable->submit_request = virtio_submit_request;
}