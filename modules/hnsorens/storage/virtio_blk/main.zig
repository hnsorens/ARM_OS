//! VirtIO block device driver, exporting `BlkDevice` (category
//! "blkdevice"). Ported from `things_to_add/blk_device.c`: finishes the
//! device-specific half of VirtIO setup that `VirtioBus.init_device`
//! deliberately leaves undone (queue 0 setup, reading the config space,
//! `DRIVER_OK`), then serves reads/writes/flush as single-request-at-a-
//! time `VirtioBus.submit_request` calls.
//!
//! All addressing (`read_sectors`/`write_sectors`' `lba`, `GptPartition`'s
//! LBAs) is in fixed 512-byte sectors, the VirtIO block spec's unit --
//! independent of the device's reported optimal `blk_size`.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");

const virtiobus_if = abi.importInterface(abi.VirtioBus);
pub const pmm_if = abi.importInterface(abi.Pmm);
pub const serial_if = abi.importInterface(abi.Serial);

const queue_size_config = abi.declareConfigInt("queue_size", u16, 8);

pub const SECTOR_SIZE: u64 = 512;

const VIRTIO_BLK_T_IN: u32 = 0;
const VIRTIO_BLK_T_OUT: u32 = 1;
const VIRTIO_BLK_T_FLUSH: u32 = 4;

const VIRTIO_BLK_F_FLUSH_BIT: u5 = 9;
const VIRTIO_BLK_F_RO_BIT: u5 = 5;

const VIRTIO_MMIO_VERSION: u64 = 0x004;
const VIRTIO_MMIO_DEVICE_FEATURES: u64 = 0x010;
const VIRTIO_MMIO_DEVICE_FEATURES_SEL: u64 = 0x014;
const VIRTIO_MMIO_STATUS: u64 = 0x070;
const VIRTIO_MMIO_CONFIG: u64 = 0x100;

const STATUS_DRIVER_OK: u32 = 0x04;

fn reg32(mmio_base: u64, offset: u64) *volatile u32 {
    return @ptrFromInt(mmio_base + offset);
}

/// Device-specific config space at offset 0x100 (virtio-blk spec section
/// 5.2.4). Little-endian, matching this CPU's native endianness -- no
/// byte-swap needed.
const BlkConfig = extern struct {
    capacity: u64,
    size_max: u32,
    seg_max: u32,
    geo_cylinders: u16,
    geo_heads: u8,
    geo_sectors: u8,
    blk_size: u32,
    topo_physical_block_exp: u8,
    topo_alignment_offset: u8,
    topo_min_io_size: u16,
    topo_opt_io_size: u32,
    writeback: u8,
};

const Device = struct {
    mmio_base: u64 = 0,
    queue: abi.VirtioQueue = .{},
    capacity_sectors: u64 = 0,
    read_only: bool = false,
    flush_supported: bool = false,
};

/// Backing storage for device instances -- a fixed table rather than a
/// heap allocation, matching this module's "no general-purpose heap"
/// design (see `phys_mem.zig`'s doc comment); one VirtIO block device is
/// already more than this OS currently attaches.
const MAX_DEVICES: usize = 4;
var s_devices: [MAX_DEVICES]Device = [_]Device{.{}} ** MAX_DEVICES;
var s_device_count: usize = 0;

fn asDev(ptr: ?*anyopaque) ?*Device {
    return @ptrCast(@alignCast(ptr));
}

fn findDeviceSlot(mmio_base: u64) ?*Device {
    for (s_devices[0..s_device_count]) |*existing| {
        if (existing.mmio_base == mmio_base) return existing;
    }
    return null;
}

pub fn create(mmio_base: u64, out_dev: *?*anyopaque) callconv(.c) c_int {
    if (mmio_base == 0) return abi.EINVAL;

    if (reg32(mmio_base, VIRTIO_MMIO_VERSION).* != 2) return abi.EIO;

    reg32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES_SEL).* = 1;
    const features_high = reg32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES).*;
    if ((features_high >> 0) & 1 == 0) return abi.EIO; // VERSION_1, bit 32 overall

    // Reuses an existing slot for this mmio_base rather than allocating a
    // new one (this OS has no device-open refcounting layer yet, so
    // several independent callers legitimately `create()` the same
    // physical device -- e.g. every module that mounts the root
    // filesystem for its own tests), but always redoes the full
    // negotiation/`setup_queue`/DRIVER_OK sequence below regardless of
    // whether this is a first or repeat call. Skipping that on a repeat
    // call would be wrong the moment *anything* else reset the device in
    // between (any `VirtioBus.init_device` call does, unconditionally) --
    // the device would silently go back to not-DRIVER_OK, disabled-queue
    // state while this slot's software copy still claimed it was live.
    var dev: *Device = undefined;
    if (findDeviceSlot(mmio_base)) |existing| {
        dev = existing;
    } else {
        if (s_device_count >= MAX_DEVICES) return abi.ENOMEM;
        dev = &s_devices[s_device_count];
        s_device_count += 1;
    }
    dev.* = .{ .mmio_base = mmio_base };

    const config: *volatile BlkConfig = @ptrFromInt(mmio_base + VIRTIO_MMIO_CONFIG);
    dev.capacity_sectors = if (config.capacity != 0) config.capacity else 1024 * 1024;

    // RO and FLUSH are both in the low feature word (bits < 32).
    reg32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES_SEL).* = 0;
    const features_low = reg32(mmio_base, VIRTIO_MMIO_DEVICE_FEATURES).*;
    dev.read_only = (features_low >> VIRTIO_BLK_F_RO_BIT) & 1 != 0;
    dev.flush_supported = (features_low >> VIRTIO_BLK_F_FLUSH_BIT) & 1 != 0;

    dev.queue = .{ .size = queue_size_config.* };
    if (virtiobus_if.setup_queue(mmio_base, 0, &dev.queue) != 0) {
        s_device_count -= 1;
        return abi.EIO;
    }

    var status = reg32(mmio_base, VIRTIO_MMIO_STATUS).*;
    status |= STATUS_DRIVER_OK;
    reg32(mmio_base, VIRTIO_MMIO_STATUS).* = status;

    out_dev.* = dev;
    return 0;
}

fn doRequest(dev: *Device, req_type: u32, lba: u64, buf: ?*anyopaque, data_len: u64) callconv(.c) c_int {
    const ReqHeader = extern struct {
        req_type: u32 = 0,
        reserved: u32 = 0,
        sector: u64 = 0,
    };

    var req_virt: u64 = undefined;
    var req_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, @sizeOf(ReqHeader), &req_virt, &req_phys) != 0) return abi.ENOMEM;
    defer phys_mem.free(pmm_if, req_phys);

    const req: *ReqHeader = @ptrFromInt(req_virt);
    req.* = .{ .req_type = req_type, .sector = lba };

    var status_virt: u64 = undefined;
    var status_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, 1, &status_virt, &status_phys) != 0) return abi.ENOMEM;
    defer phys_mem.free(pmm_if, status_phys);

    const status_byte: *u8 = @ptrFromInt(status_virt);
    status_byte.* = 0xFF;

    const rc = virtiobus_if.submit_request(dev.mmio_base, req_type, &dev.queue, req, @sizeOf(ReqHeader), buf, data_len, status_byte);
    if (rc != 0) return rc;
    if (status_byte.* != 0) return abi.EIO;
    return 0;
}

pub fn readSectors(dev_opaque: ?*anyopaque, lba: u64, buf: ?*anyopaque, sector_count: u64) callconv(.c) c_int {
    const dev = asDev(dev_opaque) orelse return abi.EINVAL;
    if (buf == null or sector_count == 0) return abi.EINVAL;
    if (lba + sector_count > dev.capacity_sectors) return abi.EINVAL;

    return doRequest(dev, VIRTIO_BLK_T_IN, lba, buf, sector_count * SECTOR_SIZE);
}

pub fn writeSectors(dev_opaque: ?*anyopaque, lba: u64, buf: ?*const anyopaque, sector_count: u64) callconv(.c) c_int {
    const dev = asDev(dev_opaque) orelse return abi.EINVAL;
    if (buf == null or sector_count == 0) return abi.EINVAL;
    if (lba + sector_count > dev.capacity_sectors) return abi.EINVAL;
    if (dev.read_only) return abi.EIO;

    return doRequest(dev, VIRTIO_BLK_T_OUT, lba, @constCast(buf), sector_count * SECTOR_SIZE);
}

pub fn flush(dev_opaque: ?*anyopaque) callconv(.c) c_int {
    const dev = asDev(dev_opaque) orelse return abi.EINVAL;
    if (!dev.flush_supported) return 0;

    return doRequest(dev, VIRTIO_BLK_T_FLUSH, 0, null, 0);
}

pub fn getCapacitySectors(dev_opaque: ?*anyopaque, sectors_out: *u64) callconv(.c) c_int {
    const dev = asDev(dev_opaque) orelse return abi.EINVAL;
    sectors_out.* = dev.capacity_sectors;
    return 0;
}

comptime {
    abi.exportInterface("blkdevice", abi.BlkDevice, .{
        .create = create,
        .read_sectors = readSectors,
        .write_sectors = writeSectors,
        .flush = flush,
        .get_capacity_sectors = getCapacitySectors,
    });
}

comptime {
    _ = @import("test.zig");
}
