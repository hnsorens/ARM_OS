//! Tests for the VirtIO block device driver in `main.zig`, split into
//! their own file (reachable from the build via `main.zig`'s `comptime {
//! _ = @import("test.zig"); }`).
//!
//! Exercises the real `virtio-blk-device` QEMU attaches in the test
//! harness (see the root `build.zig`), not a mock -- same approach as
//! `virtio_bus/test.zig`.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");
const main = @import("main.zig");

const serial_if = main.serial_if;

const VIRTIO_MMIO_BASE: u64 = 0x0A000000;
const VIRTIO_MMIO_END: u64 = 0x0A004000;
const VIRTIO_MMIO_STRIDE: u64 = 0x200;
const VIRTIO_MAGIC: u32 = 0x74726976;
const VIRTIO_BLK_DEVICE_ID: u32 = 2;

// This module only imports `VirtioBus` transitively through `main.zig`'s
// own import (not re-exported), so tests re-scan for the device base and
// run the common handshake directly against the raw registers -- the
// generic transport's own `find_device`/`init_device` are covered by
// `virtio_bus/test.zig`; these tests are about `create`/read/write/flush.
//
// The handshake (and `main.create`) must run exactly once for the whole
// test run, not once per test function: `status_reg.* = 0` is a full
// VirtIO device reset, and `main.create` is idempotent per mmio_base (see
// its doc comment) -- it will *not* redo queue setup/DRIVER_OK on a
// second call for an already-created device, so resetting the device
// again after that would leave it silently dead (queue disabled, no
// DRIVER_OK) while every test still believed it was live. Caching the
// opened device here is what avoids that.
var s_test_dev: ?*anyopaque = null;

fn getTestDevice() ?*anyopaque {
    if (s_test_dev) |d| return d;

    const base = findVirtioBlkBase();
    if (base == 0) return null;
    runCommonHandshake(base);

    var dev: ?*anyopaque = null;
    if (main.create(base, &dev) != 0) return null;
    s_test_dev = dev;
    return dev;
}

fn findVirtioBlkBase() u64 {
    var base = VIRTIO_MMIO_BASE;
    while (base < VIRTIO_MMIO_END) : (base += VIRTIO_MMIO_STRIDE) {
        const magic: *volatile u32 = @ptrFromInt(base + 0x000);
        if (magic.* != VIRTIO_MAGIC) continue;
        const device_id: *volatile u32 = @ptrFromInt(base + 0x008);
        if (device_id.* == VIRTIO_BLK_DEVICE_ID) return base;
    }
    return 0;
}

fn runCommonHandshake(base: u64) void {
    const status_reg: *volatile u32 = @ptrFromInt(base + 0x070);
    status_reg.* = 0;
    status_reg.* = 0x01;
    status_reg.* |= 0x02;

    const feat_sel: *volatile u32 = @ptrFromInt(base + 0x014);
    const feat: *volatile u32 = @ptrFromInt(base + 0x010);
    feat_sel.* = 1;
    const high = feat.*;

    const drv_sel: *volatile u32 = @ptrFromInt(base + 0x024);
    const drv: *volatile u32 = @ptrFromInt(base + 0x020);
    drv_sel.* = 0;
    drv.* = 0;
    drv_sel.* = 1;
    drv.* = if (high & 1 != 0) 1 else 0;

    status_reg.* |= 0x08;
}

fn testCreateNegotiatesAndReadsConfig() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    t.expectEqual(@src(), main.getCapacitySectors(dev_opaque, &sectors), 0);
    t.expectTrue(@src(), sectors > 0);

    return t.result();
}

fn testReadSectorsRejectsOutOfRange() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    _ = main.getCapacitySectors(dev_opaque, &sectors);

    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.readSectors(dev_opaque, sectors, &buf, 1), abi.EINVAL);
    t.expectEqual(@src(), main.readSectors(dev_opaque, 0, null, 1), abi.EINVAL);
    t.expectEqual(@src(), main.readSectors(dev_opaque, 0, &buf, 0), abi.EINVAL);

    return t.result();
}

fn testWriteThenReadRoundTrips() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    // Sector 63 sits well inside the disk image (128MiB image, 512-byte
    // sectors) but outside GPT/ESP metadata (< 2048), so this can't
    // collide with the real partition layout the other modules' tests
    // read.
    const test_lba: u64 = 63;

    // BlkDevice.read_sectors/write_sectors require a physically-contiguous,
    // HHDM-mapped buffer (see abi_types.BlkDevice's doc comment) -- a plain
    // stack array is not one, and silently DMAs to/from the wrong physical
    // address instead of erroring.
    var write_virt: u64 = undefined;
    var write_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(main.pmm_if, 512, &write_virt, &write_phys), 0);
    defer phys_mem.free(main.pmm_if, write_phys);
    const write_buf: [*]u8 = @ptrFromInt(write_virt);
    for (write_buf[0..512], 0..) |*b, i| b.* = @truncate(i ^ 0xA5);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, test_lba, write_buf, 1), 0);

    var read_virt: u64 = undefined;
    var read_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(main.pmm_if, 512, &read_virt, &read_phys), 0);
    defer phys_mem.free(main.pmm_if, read_phys);
    const read_buf: [*]u8 = @ptrFromInt(read_virt);
    @memset(read_buf[0..512], 0);

    t.expectEqual(@src(), main.readSectors(dev_opaque, test_lba, read_buf, 1), 0);
    t.expectTrue(@src(), std.mem.eql(u8, write_buf[0..512], read_buf[0..512]));

    t.expectEqual(@src(), main.flush(dev_opaque), 0);

    return t.result();
}

fn allocDmaBuffer(size: u64) ?u64 {
    var virt: u64 = undefined;
    var phys: u64 = undefined;
    if (phys_mem.alloc(main.pmm_if, size, &virt, &phys) != 0) return null;
    return virt;
}

fn testCreateWithZeroBaseReturnsEinval() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var dev: ?*anyopaque = null;
    t.expectEqual(@src(), main.create(0, &dev), abi.EINVAL);
    return t.result();
}

fn testCreateOnNonMagicAddressReturnsEio() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = findVirtioBlkBase();
    t.expectTrue(@src(), base != 0);

    var dev: ?*anyopaque = null;
    // base + 0x100 is inside the same device's MMIO window but not at
    // its VERSION register -- create() checks that first.
    t.expectEqual(@src(), main.create(base + 0x100, &dev), abi.EIO);
    return t.result();
}

fn testCreateIsIdempotentReturnsSameDevice() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev1 = getTestDevice();
    t.expectTrue(@src(), dev1 != null);

    const base = findVirtioBlkBase();
    // `create` always redoes `setup_queue`/DRIVER_OK on every call (see its
    // doc comment) -- reconfiguring a *live* queue's addresses without
    // resetting the device first is undefined per the virtio-mmio spec and
    // previously corrupted the device model's virtqueue state (surfaced by
    // QEMU's own "Virtqueue size exceeded" on the next real request, not
    // by this call's own return value). A real repeat caller is expected
    // to reset via the handshake first, same as `getTestDevice` does.
    runCommonHandshake(base);
    var dev2: ?*anyopaque = null;
    t.expectEqual(@src(), main.create(base, &dev2), 0);
    t.expectEqual(@src(), dev1, dev2);
    return t.result();
}

fn testGetCapacitySectorsRejectsNullDev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var sectors: u64 = 0;
    t.expectEqual(@src(), main.getCapacitySectors(null, &sectors), abi.EINVAL);
    return t.result();
}

fn testGetCapacitySectorsIsStableAcrossCalls() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var a: u64 = 0;
    var b: u64 = 0;
    t.expectEqual(@src(), main.getCapacitySectors(dev_opaque, &a), 0);
    t.expectEqual(@src(), main.getCapacitySectors(dev_opaque, &b), 0);
    t.expectEqual(@src(), a, b);
    return t.result();
}

fn testCapacitySectorsMatchesRawConfigRegister() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const base = findVirtioBlkBase();
    const capacity_reg: *volatile u64 = @ptrFromInt(base + 0x100); // config.capacity
    var sectors: u64 = 0;
    t.expectEqual(@src(), main.getCapacitySectors(dev_opaque, &sectors), 0);
    t.expectEqual(@src(), sectors, capacity_reg.*);
    return t.result();
}

fn testReadSectorsRejectsNullDev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.readSectors(null, 0, &buf, 1), abi.EINVAL);
    return t.result();
}

fn testWriteSectorsRejectsNullDev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.writeSectors(null, 0, &buf, 1), abi.EINVAL);
    return t.result();
}

fn testWriteSectorsRejectsNullBuf() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);
    t.expectEqual(@src(), main.writeSectors(dev_opaque, 0, null, 1), abi.EINVAL);
    return t.result();
}

fn testWriteSectorsRejectsZeroCount() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);
    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.writeSectors(dev_opaque, 0, &buf, 0), abi.EINVAL);
    return t.result();
}

fn testWriteSectorsRejectsOutOfRange() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    _ = main.getCapacitySectors(dev_opaque, &sectors);

    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.writeSectors(dev_opaque, sectors, &buf, 1), abi.EINVAL);
    return t.result();
}

fn testReadWriteAtSectorZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    // `readSectors` requires a physically-contiguous, HHDM-mapped buffer
    // (see `BlkDevice`'s doc comment) -- a plain stack array is not one,
    // and silently DMAs to/from a bogus physical address instead of
    // erroring, which previously corrupted the shared virtqueue and wedged
    // every test after this one for the rest of the file.
    const virt = allocDmaBuffer(512);
    t.expectTrue(@src(), virt != null);
    const buf: [*]u8 = @ptrFromInt(virt.?);
    t.expectEqual(@src(), main.readSectors(dev_opaque, 0, buf, 1), 0);
    return t.result();
}

fn testReadWriteAtLastValidSector() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    _ = main.getCapacitySectors(dev_opaque, &sectors);

    const write_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), write_virt != null);
    const write_buf: [*]u8 = @ptrFromInt(write_virt.?);
    for (write_buf[0..512], 0..) |*b, i| b.* = @truncate(i ^ 0x77);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, sectors - 1, write_buf, 1), 0);

    const read_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), read_virt != null);
    const read_buf: [*]u8 = @ptrFromInt(read_virt.?);
    t.expectEqual(@src(), main.readSectors(dev_opaque, sectors - 1, read_buf, 1), 0);
    t.expectTrue(@src(), std.mem.eql(u8, write_buf[0..512], read_buf[0..512]));
    return t.result();
}

fn testReadSectorsMultiSectorRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const test_lba: u64 = 200;
    const write_virt = allocDmaBuffer(2048);
    t.expectTrue(@src(), write_virt != null);
    const write_buf: [*]u8 = @ptrFromInt(write_virt.?);
    for (write_buf[0..2048], 0..) |*b, i| b.* = @truncate(i);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, test_lba, write_buf, 4), 0);

    const read_virt = allocDmaBuffer(2048);
    t.expectTrue(@src(), read_virt != null);
    const read_buf: [*]u8 = @ptrFromInt(read_virt.?);
    @memset(read_buf[0..2048], 0);
    t.expectEqual(@src(), main.readSectors(dev_opaque, test_lba, read_buf, 4), 0);

    t.expectTrue(@src(), std.mem.eql(u8, write_buf[0..2048], read_buf[0..2048]));
    return t.result();
}

fn testReadSectorsHugeLbaOverflowRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    // A huge lba near u64's max, added to a small count, would overflow
    // a naive `lba + count > capacity` bounds check -- must be rejected
    // without panicking (see `rangeOutOfBounds`'s doc comment).
    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.readSectors(dev_opaque, std.math.maxInt(u64) - 3, &buf, 10), abi.EINVAL);
    return t.result();
}

fn testWriteSectorsHugeLbaOverflowRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.writeSectors(dev_opaque, std.math.maxInt(u64) - 3, &buf, 10), abi.EINVAL);
    return t.result();
}

fn testFlushRejectsNullDev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.flush(null), abi.EINVAL);
    return t.result();
}

fn testFlushMultipleTimesSucceeds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    t.expectEqual(@src(), main.flush(dev_opaque), 0);
    t.expectEqual(@src(), main.flush(dev_opaque), 0);
    t.expectEqual(@src(), main.flush(dev_opaque), 0);
    return t.result();
}

fn testWriteThenFlushThenReadPersists() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const test_lba: u64 = 210;
    const write_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), write_virt != null);
    const write_buf: [*]u8 = @ptrFromInt(write_virt.?);
    @memset(write_buf[0..512], 0x5C);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, test_lba, write_buf, 1), 0);
    t.expectEqual(@src(), main.flush(dev_opaque), 0);

    const read_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), read_virt != null);
    const read_buf: [*]u8 = @ptrFromInt(read_virt.?);
    t.expectEqual(@src(), main.readSectors(dev_opaque, test_lba, read_buf, 1), 0);
    t.expectTrue(@src(), std.mem.eql(u8, write_buf[0..512], read_buf[0..512]));
    return t.result();
}

fn testAdjacentSectorWritesDoNotCrossContaminate() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const lba_a: u64 = 220;
    const lba_b: u64 = 221;

    const buf_a_virt = allocDmaBuffer(512);
    const buf_b_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), buf_a_virt != null and buf_b_virt != null);
    const buf_a: [*]u8 = @ptrFromInt(buf_a_virt.?);
    const buf_b: [*]u8 = @ptrFromInt(buf_b_virt.?);
    @memset(buf_a[0..512], 0x11);
    @memset(buf_b[0..512], 0x22);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, lba_a, buf_a, 1), 0);
    t.expectEqual(@src(), main.writeSectors(dev_opaque, lba_b, buf_b, 1), 0);

    const read_a_virt = allocDmaBuffer(512);
    const read_b_virt = allocDmaBuffer(512);
    t.expectTrue(@src(), read_a_virt != null and read_b_virt != null);
    const read_a: [*]u8 = @ptrFromInt(read_a_virt.?);
    const read_b: [*]u8 = @ptrFromInt(read_b_virt.?);

    t.expectEqual(@src(), main.readSectors(dev_opaque, lba_a, read_a, 1), 0);
    t.expectEqual(@src(), main.readSectors(dev_opaque, lba_b, read_b, 1), 0);

    for (read_a[0..512]) |b| t.expectEqual(@src(), b, 0x11);
    for (read_b[0..512]) |b| t.expectEqual(@src(), b, 0x22);
    return t.result();
}

fn testMultipleSequentialWritesToDifferentSectorsAllPersist() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const lbas = [_]u64{ 230, 231, 232 };
    const patterns = [_]u8{ 0xAA, 0xBB, 0xCC };

    for (lbas, patterns) |lba, pattern| {
        const virt = allocDmaBuffer(512);
        t.expectTrue(@src(), virt != null);
        const buf: [*]u8 = @ptrFromInt(virt.?);
        @memset(buf[0..512], pattern);
        t.expectEqual(@src(), main.writeSectors(dev_opaque, lba, buf, 1), 0);
    }

    for (lbas, patterns) |lba, pattern| {
        const virt = allocDmaBuffer(512);
        t.expectTrue(@src(), virt != null);
        const buf: [*]u8 = @ptrFromInt(virt.?);
        t.expectEqual(@src(), main.readSectors(dev_opaque, lba, buf, 1), 0);
        for (buf[0..512]) |b| t.expectEqual(@src(), b, pattern);
    }
    return t.result();
}

fn testWriteSectorsRejectsOutOfRangeAtExactCapacityBoundary() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    _ = main.getCapacitySectors(dev_opaque, &sectors);

    var buf: [512]u8 = undefined;
    // lba == capacity is itself out of range (valid lbas are [0, capacity)).
    t.expectEqual(@src(), main.writeSectors(dev_opaque, sectors, &buf, 1), abi.EINVAL);
    // lba == capacity - 1 with count == 2 reaches one past the end too.
    t.expectEqual(@src(), main.writeSectors(dev_opaque, sectors - 1, &buf, 2), abi.EINVAL);
    return t.result();
}

fn testReadSectorsRejectsOutOfRangeAtExactCapacityBoundary() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    var sectors: u64 = 0;
    _ = main.getCapacitySectors(dev_opaque, &sectors);

    var buf: [512]u8 = undefined;
    t.expectEqual(@src(), main.readSectors(dev_opaque, sectors, &buf, 1), abi.EINVAL);
    t.expectEqual(@src(), main.readSectors(dev_opaque, sectors - 1, &buf, 2), abi.EINVAL);
    return t.result();
}

fn testMultiSectorWriteThenMultiSectorReadPreservesAllBytesExactly() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev_opaque = getTestDevice();
    t.expectTrue(@src(), dev_opaque != null);

    const test_lba: u64 = 240;
    const size: u64 = 8 * 512;
    const write_virt = allocDmaBuffer(size);
    t.expectTrue(@src(), write_virt != null);
    const write_buf: [*]u8 = @ptrFromInt(write_virt.?);
    for (write_buf[0..size], 0..) |*b, i| b.* = @truncate((i * 7) ^ 0x33);

    t.expectEqual(@src(), main.writeSectors(dev_opaque, test_lba, write_buf, 8), 0);

    const read_virt = allocDmaBuffer(size);
    t.expectTrue(@src(), read_virt != null);
    const read_buf: [*]u8 = @ptrFromInt(read_virt.?);
    @memset(read_buf[0..size], 0);
    t.expectEqual(@src(), main.readSectors(dev_opaque, test_lba, read_buf, 8), 0);

    var i: u64 = 0;
    while (i < size) : (i += 1) {
        t.expectEqual(@src(), read_buf[i], write_buf[i]);
    }
    return t.result();
}

comptime {
    abi.kernelTest("virtio_blk_create_negotiates_and_reads_config", &testCreateNegotiatesAndReadsConfig);
    abi.kernelTest("virtio_blk_read_sectors_rejects_out_of_range", &testReadSectorsRejectsOutOfRange);
    abi.kernelTest("virtio_blk_write_then_read_round_trips", &testWriteThenReadRoundTrips);

    abi.kernelTest("virtio_blk_create_with_zero_base_returns_einval", &testCreateWithZeroBaseReturnsEinval);
    abi.kernelTest("virtio_blk_create_on_non_magic_address_returns_eio", &testCreateOnNonMagicAddressReturnsEio);
    abi.kernelTest("virtio_blk_create_is_idempotent_returns_same_device", &testCreateIsIdempotentReturnsSameDevice);
    abi.kernelTest("virtio_blk_get_capacity_sectors_rejects_null_dev", &testGetCapacitySectorsRejectsNullDev);
    abi.kernelTest("virtio_blk_get_capacity_sectors_is_stable_across_calls", &testGetCapacitySectorsIsStableAcrossCalls);
    abi.kernelTest("virtio_blk_capacity_sectors_matches_raw_config_register", &testCapacitySectorsMatchesRawConfigRegister);
    abi.kernelTest("virtio_blk_read_sectors_rejects_null_dev", &testReadSectorsRejectsNullDev);
    abi.kernelTest("virtio_blk_write_sectors_rejects_null_dev", &testWriteSectorsRejectsNullDev);
    abi.kernelTest("virtio_blk_write_sectors_rejects_null_buf", &testWriteSectorsRejectsNullBuf);
    abi.kernelTest("virtio_blk_write_sectors_rejects_zero_count", &testWriteSectorsRejectsZeroCount);
    abi.kernelTest("virtio_blk_write_sectors_rejects_out_of_range", &testWriteSectorsRejectsOutOfRange);
    abi.kernelTest("virtio_blk_read_write_at_sector_zero", &testReadWriteAtSectorZero);
    abi.kernelTest("virtio_blk_read_write_at_last_valid_sector", &testReadWriteAtLastValidSector);
    abi.kernelTest("virtio_blk_read_sectors_multi_sector_round_trip", &testReadSectorsMultiSectorRoundTrip);
    abi.kernelTest("virtio_blk_read_sectors_huge_lba_overflow_rejected", &testReadSectorsHugeLbaOverflowRejected);
    abi.kernelTest("virtio_blk_write_sectors_huge_lba_overflow_rejected", &testWriteSectorsHugeLbaOverflowRejected);
    abi.kernelTest("virtio_blk_flush_rejects_null_dev", &testFlushRejectsNullDev);
    abi.kernelTest("virtio_blk_flush_multiple_times_succeeds", &testFlushMultipleTimesSucceeds);
    abi.kernelTest("virtio_blk_write_then_flush_then_read_persists", &testWriteThenFlushThenReadPersists);
    abi.kernelTest("virtio_blk_adjacent_sector_writes_do_not_cross_contaminate", &testAdjacentSectorWritesDoNotCrossContaminate);
    abi.kernelTest("virtio_blk_multiple_sequential_writes_to_different_sectors_all_persist", &testMultipleSequentialWritesToDifferentSectorsAllPersist);
    abi.kernelTest("virtio_blk_write_sectors_rejects_out_of_range_at_exact_capacity_boundary", &testWriteSectorsRejectsOutOfRangeAtExactCapacityBoundary);
    abi.kernelTest("virtio_blk_read_sectors_rejects_out_of_range_at_exact_capacity_boundary", &testReadSectorsRejectsOutOfRangeAtExactCapacityBoundary);
    abi.kernelTest("virtio_blk_multi_sector_write_then_multi_sector_read_preserves_all_bytes_exactly", &testMultiSectorWriteThenMultiSectorReadPreservesAllBytesExactly);
}
