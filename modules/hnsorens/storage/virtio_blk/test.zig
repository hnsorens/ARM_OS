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

comptime {
    abi.kernelTest("virtio_blk_create_negotiates_and_reads_config", &testCreateNegotiatesAndReadsConfig);
    abi.kernelTest("virtio_blk_read_sectors_rejects_out_of_range", &testReadSectorsRejectsOutOfRange);
    abi.kernelTest("virtio_blk_write_then_read_round_trips", &testWriteThenReadRoundTrips);
}
