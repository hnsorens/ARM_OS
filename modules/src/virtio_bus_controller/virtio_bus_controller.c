#include "module.h"
#include <stdbool.h>
#include <stdint.h>

#define debug "VIRTIO"

vtable(bus_controller_vtable_t)
start(init, virtio_init)

#define VIRTIO_MAGIC 0x74726976

// Safe MMIO read with debug
static uint32_t mmio_read(uintptr_t addr) {
    uint32_t value = *(volatile uint32_t*)addr;
    return value;
}

static void mmio_write(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

// Find VirtIO block device with scanning
static uintptr_t find_virtio_device(uint8_t id) {
    DEBUG("Starting VirtIO device scan...");
    
    // Scan common VirtIO MMIO addresses
    for (uintptr_t base = 0x0A000000; base < 0x0A004000; base += 0x200) {
        DEBUG("Checking address 0x%08lx", base);
        
        uint32_t magic = mmio_read(base);
        
        if (magic == VIRTIO_MAGIC) {
            DEBUG("Found VirtIO device at 0x%08lx", base);
            
            uint32_t version = mmio_read(base + 0x004);
            uint32_t device_id = mmio_read(base + 0x008);
            uint32_t vendor_id = mmio_read(base + 0x00C);
            
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

static int virtio_mmio_common_init(uintptr_t base) {
    DEBUG("=== VirtIO MMIO common init @ 0x%08lx ===", base);

    /* ------------------------------------------------------------
     * 0. Identify device
     * ------------------------------------------------------------ */
    uint32_t magic   = mmio_read(base + 0x000);
    uint32_t version = mmio_read(base + 0x004);
    uint32_t device  = mmio_read(base + 0x008);

    if (magic != 0x74726976) {
        ERROR("Invalid VirtIO magic");
        return -1;
    }

    if (version != 1 && version != 2) {
        ERROR("Unsupported VirtIO version %u", version);
        return -1;
    }

    if (device == 0) {
        ERROR("No VirtIO device present");
        return -1;
    }

    DEBUG("Device ID %u, version %u", device, version);

    // Reset device
    mmio_write(base + 0x070, 0x00);

    // ACKNOWLEDGE + DRIVER
    uint32_t status = 0;

    status |= 0x01; /* ACKNOWLEDGE */
    mmio_write(base + 0x070, status);

    status |= 0x02; /* DRIVER */
    mmio_write(base + 0x070, status);

    // Discover device features
    uint32_t dev_features[2] = {0, 0};

    for (uint32_t i = 0; i < 2; i++) {
        mmio_write(base + 0x014, i);
        dev_features[i] = mmio_read(base + 0x010);
        DEBUG("Device features[%u] = 0x%08x", i, dev_features[i]);
    }

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
        mmio_write(base + 0x024, i);
        mmio_write(base + 0x020, drv_features[i]);
    }

    // FEATURES_OK handshake
    status |= 0x08; /* FEATURES_OK */
    mmio_write(base + 0x070, status);

    status = mmio_read(base + 0x070);
    if (!(status & 0x08)) {
        ERROR("Device rejected negotiated features");
        return -1;
    }

    // Common init complete
    DEBUG("VirtIO common init complete");
    DEBUG("Queues MUST be configured next by device driver");

    /*
     * DO NOT:
     *  - touch queues
     *  - set DRIVER_OK
     *  - access device-specific config space
     */

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

void virtio_init_device(uintptr_t device_base)
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

void virtio_init(kernel_vtable_t* kvtable) 
{

}

void init(bus_controller_vtable_t* vtable) 
{
    vtable->init_device = virtio_init_device;
    vtable->find_device = virtio_find_device;
}