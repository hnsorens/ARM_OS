//! Tests for the VirtIO-MMIO transport in `main.zig`, split into their own
//! file (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! QEMU's test harness (`zig build qemu-test`) always launches with
//! `-device virtio-blk-device,drive=d0` (see the root `build.zig`), so
//! these exercise a real device at QEMU virt's VirtIO-MMIO window, not a
//! mock -- same approach as `gic_v3/test.zig` against real GICv3 hardware.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");
const main = @import("main.zig");

const serial_if = main.serial_if;
const pmm_if = main.pmm_if;

const VIRTIO_BLK_DEVICE_ID: u32 = 2;
const STATUS_REG_OFFSET: u64 = 0x070;
const STATUS_FEATURES_OK: u32 = 0x08;
const STATUS_DRIVER_OK: u32 = 0x04;

fn testFindDeviceLocatesVirtioBlk() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    return t.result();
}

fn testFindDeviceMissingIdReturnsZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.findDevice(0xFFFF), 0);
    return t.result();
}

fn testInitDeviceHandshakeSucceeds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    t.expectEqual(@src(), main.initDevice(base), 0);

    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    t.expectTrue(@src(), status_reg.* & STATUS_FEATURES_OK != 0);
    return t.result();
}

fn testInitDeviceRejectsZeroBase() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.initDevice(0), abi.EINVAL);
    return t.result();
}

fn testSetupQueueAllocatesRings() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    var queue: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);
    t.expectEqual(@src(), queue.size, 8);
    t.expectTrue(@src(), queue.desc != 0);
    t.expectTrue(@src(), queue.avail != 0);
    t.expectTrue(@src(), queue.used != 0);
    t.expectEqual(@src(), queue.free_head, 0);
    return t.result();
}

fn testSubmitRequestReadsBootSector() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    _ = main.initDevice(base);

    var queue: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);

    // Bring the device fully up (DRIVER_OK) -- init_device deliberately
    // stops short of this; a real driver (virtio_blk) does it after its
    // own queue/config setup.
    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    status_reg.* |= STATUS_DRIVER_OK;

    var req_virt: u64 = undefined;
    var req_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, @sizeOf(ReqHeader), &req_virt, &req_phys), 0);
    const req: *ReqHeader = @ptrFromInt(req_virt);
    req.* = .{};

    var data_virt: u64 = undefined;
    var data_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, 512, &data_virt, &data_phys), 0);
    const data_ptr: *anyopaque = @ptrFromInt(data_virt);

    var status_virt: u64 = undefined;
    var status_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, 1, &status_virt, &status_phys), 0);
    const status_byte: *u8 = @ptrFromInt(status_virt);
    status_byte.* = 0xFF;

    t.expectEqual(@src(), main.submitRequest(base, 0, &queue, req, @sizeOf(ReqHeader), data_ptr, 512, status_byte), 0);
    t.expectEqual(@src(), status_byte.*, 0);

    return t.result();
}

// Raw VirtIO virtqueue wire layout (spec-fixed, not this driver's private
// types) -- used by tests that inspect ring memory directly rather than
// only through the ABI's return values.
const RawDesc = extern struct {
    addr: u64,
    len: u32,
    flags: u16,
    next: u16,
};
const RawRingHeader = extern struct {
    flags: u16,
    idx: u16,
};

const ReqHeader = extern struct {
    req_type: u32 = 0,
    reserved: u32 = 0,
    sector: u64 = 0,
};

/// Finds the device, resets and renegotiates it, sets up queue 0 at
/// `size`, and brings it fully up (DRIVER_OK) -- everything a test that
/// isn't itself about `find_device`/`init_device`/`setup_queue` needs
/// before it can `submit_request`. Every call does a full device reset
/// (via `init_device`), matching the existing tests' self-contained
/// style rather than caching state across tests -- see `virtio_blk`'s
/// `create()` doc comment for why silently reusing a *possibly-reset*
/// device is actively dangerous, not just wasteful.
fn setupReadyQueue(queue: *abi.VirtioQueue, size: u16) ?u64 {
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    if (base == 0) return null;
    if (main.initDevice(base) != 0) return null;
    queue.* = .{ .size = size };
    if (main.setupQueue(base, 0, queue) != 0) return null;
    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    status_reg.* |= STATUS_DRIVER_OK;
    return base;
}

fn allocDmaBuffer(comptime size: u64) ?u64 {
    var virt: u64 = undefined;
    var phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, size, &virt, &phys) != 0) return null;
    return virt;
}

fn testFindDeviceIsIdempotentAcrossCalls() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const first = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    const second = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), first != 0);
    t.expectEqual(@src(), first, second);
    return t.result();
}

fn testFindDeviceRejectsSeveralMissingIds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    // Device id 0 is deliberately excluded: the virtio-mmio spec reserves
    // it as the "no device present" sentinel, so any unused slot in the
    // scanned range legitimately reports it -- `findDevice(0)` correctly
    // matches the first such empty slot rather than genuinely being
    // "missing" (see `initDevice`'s own `DeviceId == 0` -> EIO check).
    const missing_ids = [_]u32{ 1, 3, 4, 99, 0xFFFE };
    for (missing_ids) |id| {
        t.expectEqual(@src(), main.findDevice(id), 0);
    }
    return t.result();
}

fn testInitDeviceCanBeCalledAgainAfterSuccess() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    t.expectEqual(@src(), main.initDevice(base), 0);
    t.expectEqual(@src(), main.initDevice(base), 0);
    return t.result();
}

fn testInitDeviceSetsAcknowledgeAndDriverBits() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectEqual(@src(), main.initDevice(base), 0);

    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    t.expectTrue(@src(), status_reg.* & 0x01 != 0); // ACKNOWLEDGE
    t.expectTrue(@src(), status_reg.* & 0x02 != 0); // DRIVER
    return t.result();
}

fn testInitDeviceDoesNotSetDriverOk() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectEqual(@src(), main.initDevice(base), 0);

    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    t.expectTrue(@src(), status_reg.* & STATUS_DRIVER_OK == 0);
    return t.result();
}

fn testInitDeviceOnNonMagicAddressReturnsEio() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    // base + 0x100 lands inside the same device's own MMIO window but
    // not at its magic register -- whatever's actually there won't spell
    // "virt" by coincidence.
    t.expectEqual(@src(), main.initDevice(base + 0x100), abi.EIO);
    return t.result();
}

fn testInitDeviceHandshakeIdempotentMultipleResets() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectEqual(@src(), main.initDevice(base), 0);
    t.expectEqual(@src(), main.initDevice(base), 0);
    t.expectEqual(@src(), main.initDevice(base), 0);

    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    t.expectTrue(@src(), status_reg.* & STATUS_FEATURES_OK != 0);
    return t.result();
}

fn testSetupQueueDefaultsToMaxWhenSizeZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    const queue_num_max_reg: *volatile u32 = @ptrFromInt(base + 0x034);
    const queue_sel_reg: *volatile u32 = @ptrFromInt(base + 0x030);
    queue_sel_reg.* = 0;
    const max_size = queue_num_max_reg.*;
    t.expectTrue(@src(), max_size > 0);

    var queue: abi.VirtioQueue = .{ .size = 0 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);
    t.expectEqual(@src(), queue.size, @as(u16, @truncate(max_size)));
    return t.result();
}

fn testSetupQueueClampsToRequestedWhenBelowMax() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    var queue: abi.VirtioQueue = .{ .size = 4 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);
    t.expectEqual(@src(), queue.size, 4);
    return t.result();
}

fn testSetupQueueClampsToMaxWhenAboveMax() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    const queue_num_max_reg: *volatile u32 = @ptrFromInt(base + 0x034);
    const queue_sel_reg: *volatile u32 = @ptrFromInt(base + 0x030);
    queue_sel_reg.* = 0;
    const max_size = queue_num_max_reg.*;

    var queue: abi.VirtioQueue = .{ .size = 60000 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);
    t.expectEqual(@src(), queue.size, @as(u16, @truncate(max_size)));
    return t.result();
}

fn testSetupQueueFreeListChainsCorrectly() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const table: [*]const RawDesc = @ptrFromInt(queue.desc);
    var i: u16 = 0;
    while (i < queue.size - 1) : (i += 1) {
        t.expectEqual(@src(), table[i].next, i + 1);
    }
    t.expectEqual(@src(), table[queue.size - 1].next, 0xFFFF);
    t.expectEqual(@src(), queue.free_head, 0);
    return t.result();
}

fn testSetupQueueDescAvailUsedAreDistinctBuffers() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    t.expectTrue(@src(), queue.desc != queue.avail);
    t.expectTrue(@src(), queue.desc != queue.used);
    t.expectTrue(@src(), queue.avail != queue.used);
    return t.result();
}

fn testSetupQueueOnInvalidQueueIndexReturnsEio() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    // This device only implements queue 0 -- QUEUE_NUM_MAX for any other
    // index reads back 0, which setup_queue must treat as "not available".
    var queue: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 1, &queue), abi.EIO);
    return t.result();
}

fn testSetupQueueCanBeCalledAgainReinitializing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    var queue1: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue1), 0);

    var queue2: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue2), 0);
    t.expectEqual(@src(), queue2.free_head, 0);
    return t.result();
}

fn testSetupQueueZeroesDescriptorTableInitially() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const table: [*]const RawDesc = @ptrFromInt(queue.desc);
    t.expectEqual(@src(), table[0].addr, 0);
    t.expectEqual(@src(), table[0].len, 0);
    t.expectEqual(@src(), table[0].flags, 0);
    t.expectEqual(@src(), table[0].next, 1);
    return t.result();
}

fn testAvailHeaderStartsAtZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const avail_header: *const RawRingHeader = @ptrFromInt(queue.avail);
    t.expectEqual(@src(), avail_header.idx, 0);
    t.expectEqual(@src(), queue.last_used_idx, 0);
    return t.result();
}

fn testSubmitRequestWriteThenReadRoundTripRaw() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    // Sector 80: distinct from virtio_blk's own round-trip test (63) and
    // ext2/gpt's real partition data (>= 2048), so nothing else observes
    // whatever this test leaves there.
    const test_lba: u64 = 80;

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);

    const write_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), write_virt != null);
    const write_buf: [*]u8 = @ptrFromInt(write_virt.?);
    for (write_buf[0..512], 0..) |*b, i| b.* = @truncate(i ^ 0x3C);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);
    status_byte.* = 0xFF;

    req.* = .{ .req_type = 1, .sector = test_lba }; // VIRTIO_BLK_T_OUT
    t.expectEqual(@src(), main.submitRequest(base.?, 1, &queue, req, @sizeOf(ReqHeader), write_buf, 512, status_byte), 0);
    t.expectEqual(@src(), status_byte.*, 0);

    const read_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), read_virt != null);
    const read_buf: [*]u8 = @ptrFromInt(read_virt.?);
    @memset(read_buf[0..512], 0);
    status_byte.* = 0xFF;

    req.* = .{ .req_type = 0, .sector = test_lba }; // VIRTIO_BLK_T_IN
    t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), read_buf, 512, status_byte), 0);
    t.expectEqual(@src(), status_byte.*, 0);

    var i: usize = 0;
    while (i < 512) : (i += 1) {
        t.expectEqual(@src(), read_buf[i], write_buf[i]);
    }
    return t.result();
}

fn testSubmitRequestFlushStyleNoDataRequest() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);
    req.* = .{ .req_type = 4, .sector = 0 }; // VIRTIO_BLK_T_FLUSH

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);
    status_byte.* = 0xFF;

    // No data descriptor: `data` is null, matching a real flush command's
    // header-then-status-only descriptor chain (see `submit_request`'s
    // `has_data` handling).
    t.expectEqual(@src(), main.submitRequest(base.?, 4, &queue, req, @sizeOf(ReqHeader), null, 0, status_byte), 0);
    t.expectEqual(@src(), status_byte.*, 0);
    return t.result();
}

fn testSubmitRequestReturnsEnomemWhenDescriptorsExhausted() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    // Simulate an exhausted free list directly -- reachable in principle
    // if enough requests were ever in flight concurrently at once (this
    // driver never does that, but `submit_request` still needs to fail
    // cleanly rather than corrupt memory if it ever happened).
    queue.free_head = 0xFFFF;

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);
    req.* = .{};

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    t.expectEqual(@src(), main.submitRequest(base.?, 4, &queue, req, @sizeOf(ReqHeader), null, 0, status_byte), abi.ENOMEM);
    return t.result();
}

fn testSubmitRequestRejectsNullReq() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    t.expectEqual(@src(), main.submitRequest(base.?, 4, &queue, null, 0, null, 0, status_byte), abi.EINVAL);
    return t.result();
}

fn testSubmitRequestRejectsNullStatus() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);
    req.* = .{};

    t.expectEqual(@src(), main.submitRequest(base.?, 4, &queue, req, @sizeOf(ReqHeader), null, 0, null), abi.EINVAL);
    return t.result();
}

fn testSubmitRequestConsecutiveCallsDoNotLeakDescriptors() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);

    const data_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), data_virt != null);
    const data_buf: *anyopaque = @ptrFromInt(data_virt.?);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    var i: u32 = 0;
    while (i < 5) : (i += 1) {
        req.* = .{ .req_type = 0, .sector = 0 };
        status_byte.* = 0xFF;
        t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), data_buf, 512, status_byte), 0);
        t.expectEqual(@src(), status_byte.*, 0);
    }
    // If descriptors leaked, the free list would be exhausted well before
    // 5 rounds of a 3-descriptor request against an 8-entry queue.
    t.expectTrue(@src(), queue.free_head != 0xFFFF);
    return t.result();
}

fn testSubmitRequestPreservesQueueSizeAcrossRequests() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);
    req.* = .{ .req_type = 0, .sector = 0 };

    const data_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), data_virt != null);
    const data_buf: *anyopaque = @ptrFromInt(data_virt.?);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), data_buf, 512, status_byte), 0);
    t.expectEqual(@src(), queue.size, 8);
    return t.result();
}

fn testUsedIdxAdvancesAfterEachRequest() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const used_header: *const RawRingHeader = @ptrFromInt(queue.used);
    const before = used_header.idx;

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);
    req.* = .{ .req_type = 0, .sector = 0 };

    const data_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), data_virt != null);
    const data_buf: *anyopaque = @ptrFromInt(data_virt.?);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), data_buf, 512, status_byte), 0);
    t.expectEqual(@src(), used_header.idx, before +% 1);
    t.expectEqual(@src(), queue.last_used_idx, 1);
    return t.result();
}

fn testSubmitRequestReadSameSectorTwiceIsConsistent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var queue: abi.VirtioQueue = undefined;
    const base = setupReadyQueue(&queue, 8);
    t.expectTrue(@src(), base != null);

    const req_virt = allocDmaBuffer(@sizeOf(ReqHeader));
    t.expectTrue(@src(), req_virt != null);
    const req: *ReqHeader = @ptrFromInt(req_virt.?);

    const buf1_virt = allocDmaBuffer(512);
    const buf2_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), buf1_virt != null and buf2_virt != null);
    const buf1: [*]u8 = @ptrFromInt(buf1_virt.?);
    const buf2: [*]u8 = @ptrFromInt(buf2_virt.?);

    const status_virt = allocDmaBuffer(1);
    t.expectTrue(@src(), status_virt != null);
    const status_byte: *u8 = @ptrFromInt(status_virt.?);

    req.* = .{ .req_type = 0, .sector = 0 };
    status_byte.* = 0xFF;
    t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), buf1, 512, status_byte), 0);

    req.* = .{ .req_type = 0, .sector = 0 };
    status_byte.* = 0xFF;
    t.expectEqual(@src(), main.submitRequest(base.?, 0, &queue, req, @sizeOf(ReqHeader), buf2, 512, status_byte), 0);

    var i: usize = 0;
    while (i < 512) : (i += 1) {
        t.expectEqual(@src(), buf1[i], buf2[i]);
    }
    return t.result();
}

comptime {
    abi.kernelTest("virtio_bus_find_device_locates_virtio_blk", &testFindDeviceLocatesVirtioBlk);
    abi.kernelTest("virtio_bus_find_device_missing_id_returns_zero", &testFindDeviceMissingIdReturnsZero);
    abi.kernelTest("virtio_bus_init_device_handshake_succeeds", &testInitDeviceHandshakeSucceeds);
    abi.kernelTest("virtio_bus_init_device_rejects_zero_base", &testInitDeviceRejectsZeroBase);
    abi.kernelTest("virtio_bus_setup_queue_allocates_rings", &testSetupQueueAllocatesRings);
    abi.kernelTest("virtio_bus_submit_request_reads_boot_sector", &testSubmitRequestReadsBootSector);

    abi.kernelTest("virtio_bus_find_device_is_idempotent_across_calls", &testFindDeviceIsIdempotentAcrossCalls);
    abi.kernelTest("virtio_bus_find_device_rejects_several_missing_ids", &testFindDeviceRejectsSeveralMissingIds);
    abi.kernelTest("virtio_bus_init_device_can_be_called_again_after_success", &testInitDeviceCanBeCalledAgainAfterSuccess);
    abi.kernelTest("virtio_bus_init_device_sets_acknowledge_and_driver_bits", &testInitDeviceSetsAcknowledgeAndDriverBits);
    abi.kernelTest("virtio_bus_init_device_does_not_set_driver_ok", &testInitDeviceDoesNotSetDriverOk);
    abi.kernelTest("virtio_bus_init_device_on_non_magic_address_returns_eio", &testInitDeviceOnNonMagicAddressReturnsEio);
    abi.kernelTest("virtio_bus_init_device_handshake_idempotent_multiple_resets", &testInitDeviceHandshakeIdempotentMultipleResets);

    abi.kernelTest("virtio_bus_setup_queue_defaults_to_max_when_size_zero", &testSetupQueueDefaultsToMaxWhenSizeZero);
    abi.kernelTest("virtio_bus_setup_queue_clamps_to_requested_when_below_max", &testSetupQueueClampsToRequestedWhenBelowMax);
    abi.kernelTest("virtio_bus_setup_queue_clamps_to_max_when_above_max", &testSetupQueueClampsToMaxWhenAboveMax);
    abi.kernelTest("virtio_bus_setup_queue_free_list_chains_correctly", &testSetupQueueFreeListChainsCorrectly);
    abi.kernelTest("virtio_bus_setup_queue_desc_avail_used_are_distinct_buffers", &testSetupQueueDescAvailUsedAreDistinctBuffers);
    abi.kernelTest("virtio_bus_setup_queue_on_invalid_queue_index_returns_eio", &testSetupQueueOnInvalidQueueIndexReturnsEio);
    abi.kernelTest("virtio_bus_setup_queue_can_be_called_again_reinitializing", &testSetupQueueCanBeCalledAgainReinitializing);
    abi.kernelTest("virtio_bus_setup_queue_zeroes_descriptor_table_initially", &testSetupQueueZeroesDescriptorTableInitially);
    abi.kernelTest("virtio_bus_avail_header_starts_at_zero", &testAvailHeaderStartsAtZero);

    abi.kernelTest("virtio_bus_submit_request_write_then_read_round_trip_raw", &testSubmitRequestWriteThenReadRoundTripRaw);
    abi.kernelTest("virtio_bus_submit_request_flush_style_no_data_request", &testSubmitRequestFlushStyleNoDataRequest);
    abi.kernelTest("virtio_bus_submit_request_returns_enomem_when_descriptors_exhausted", &testSubmitRequestReturnsEnomemWhenDescriptorsExhausted);
    abi.kernelTest("virtio_bus_submit_request_rejects_null_req", &testSubmitRequestRejectsNullReq);
    abi.kernelTest("virtio_bus_submit_request_rejects_null_status", &testSubmitRequestRejectsNullStatus);
    abi.kernelTest("virtio_bus_submit_request_consecutive_calls_do_not_leak_descriptors", &testSubmitRequestConsecutiveCallsDoNotLeakDescriptors);
    abi.kernelTest("virtio_bus_submit_request_preserves_queue_size_across_requests", &testSubmitRequestPreservesQueueSizeAcrossRequests);
    abi.kernelTest("virtio_bus_used_idx_advances_after_each_request", &testUsedIdxAdvancesAfterEachRequest);
    abi.kernelTest("virtio_bus_submit_request_read_same_sector_twice_is_consistent", &testSubmitRequestReadSameSectorTwiceIsConsistent);
}
