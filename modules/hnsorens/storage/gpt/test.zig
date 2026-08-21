//! Tests for the GPT partition table reader in `main.zig`, split into
//! their own file (reachable from the build via `main.zig`'s `comptime {
//! _ = @import("test.zig"); }`).
//!
//! Reads the real `disk.img` the QEMU test harness boots from (see the
//! root `build.zig`), which sgdisk partitions with a well-known layout --
//! partition 0 is always the EFI System Partition at LBA 2048.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

const VIRTIO_BLK_DEVICE_ID: u32 = 2;

// EFI System Partition type GUID (C12A7328-F81F-11D2-BA4B-00A0C93EC93B),
// as its mixed-endian on-disk byte encoding.
const ESP_TYPE_GUID: [16]u8 = .{ 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b };

// Cached, not re-opened per test: `virtiobus_if.init_device` performs a
// full VirtIO device reset, and `blkdevice_if.create` is idempotent per
// mmio_base (see its doc comment) -- it won't redo queue setup/DRIVER_OK
// on a second call for an already-created device, so resetting the
// device again after that would leave it silently dead while every test
// still believed it was live.
var s_test_dev: ?*anyopaque = null;

fn openTestDevice() ?*anyopaque {
    if (s_test_dev) |d| return d;

    const base = main.virtiobus_if.find_device(VIRTIO_BLK_DEVICE_ID);
    if (base == 0) return null;
    if (main.virtiobus_if.init_device(base) != 0) return null;

    var dev: ?*anyopaque = null;
    if (main.blkdevice_if.create(base, &dev) != 0) return null;
    s_test_dev = dev;
    return dev;
}

fn testReadPartitionsFindsEsp() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 1);
    t.expectEqual(@src(), partitions[0].first_lba, 2048);
    t.expectTrue(@src(), std.mem.eql(u8, &partitions[0].type_guid, &ESP_TYPE_GUID));

    return t.result();
}

fn testReadPartitionsRejectsBadArgs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var partitions: [4]abi.GptPartition = undefined;
    var count: u32 = 0;

    t.expectEqual(@src(), main.readPartitions(null, &partitions, partitions.len, &count), abi.EINVAL);
    t.expectEqual(@src(), main.readPartitions(@ptrFromInt(0x1000), &partitions, 0, &count), abi.EINVAL);

    return t.result();
}

fn testReadPartitionsRespectsMaxPartitions() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [1]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectEqual(@src(), count, 1);

    return t.result();
}

comptime {
    abi.kernelTest("gpt_read_partitions_finds_esp", &testReadPartitionsFindsEsp);
    abi.kernelTest("gpt_read_partitions_rejects_bad_args", &testReadPartitionsRejectsBadArgs);
    abi.kernelTest("gpt_read_partitions_respects_max_partitions", &testReadPartitionsRespectsMaxPartitions);
}
