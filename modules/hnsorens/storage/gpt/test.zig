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

// Linux filesystem data type GUID (0FC63DAF-8483-4772-8E79-3D69D8477DE4),
// mixed-endian on-disk encoding -- the type sgdisk's `-t 2:8300` assigns.
const ROOTFS_TYPE_GUID: [16]u8 = .{ 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 };

// Known-fixed layout from the root `build.zig`'s `sgdisk_part` invocation:
// partition 1 (ESP) spans LBA [2048, 133119], partition 2 (rootfs) spans
// [133120, 262110] -- contiguous, no gap.
const ESP_FIRST_LBA: u64 = 2048;
const ESP_LAST_LBA: u64 = 133119;
const ROOTFS_FIRST_LBA: u64 = 133120;
const ROOTFS_LAST_LBA: u64 = 262110;
const DISK_TOTAL_SECTORS: u64 = (128 * 1024 * 1024) / 512;

fn testReadPartitionsRejectsNullDevWithValidMax() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var partitions: [4]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(null, &partitions, partitions.len, &count), abi.EINVAL);
    return t.result();
}

fn testReadPartitionsRejectsZeroMaxWithValidDev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [4]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, 0, &count), abi.EINVAL);
    return t.result();
}

fn testReadPartitionsRejectsBothNullDevAndZeroMax() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var partitions: [4]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(null, &partitions, 0, &count), abi.EINVAL);
    return t.result();
}

fn testReadPartitionsCountOutIsZeroedEvenOnEarlyEinval() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [4]abi.GptPartition = undefined;

    // Seed count_out with a stale value from a real prior successful call,
    // then make an invalid call and confirm it's reset to 0 rather than
    // left showing the last real result.
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count > 0);

    t.expectEqual(@src(), main.readPartitions(null, &partitions, partitions.len, &count), abi.EINVAL);
    t.expectEqual(@src(), count, 0);

    t.expectEqual(@src(), main.readPartitions(dev, &partitions, 0, &count), abi.EINVAL);
    t.expectEqual(@src(), count, 0);

    return t.result();
}

fn testReadPartitionsFindsRootfsPartition() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    t.expectEqual(@src(), partitions[1].first_lba, ROOTFS_FIRST_LBA);
    t.expectEqual(@src(), partitions[1].last_lba, ROOTFS_LAST_LBA);
    t.expectTrue(@src(), std.mem.eql(u8, &partitions[1].type_guid, &ROOTFS_TYPE_GUID));

    return t.result();
}

fn testReadPartitionsCountIsExactlyTwoWithAmpleCapacity() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectEqual(@src(), count, 2);

    return t.result();
}

fn testReadPartitionsOrderMatchesTableIndex() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    t.expectTrue(@src(), std.mem.eql(u8, &partitions[0].type_guid, &ESP_TYPE_GUID));
    t.expectTrue(@src(), std.mem.eql(u8, &partitions[1].type_guid, &ROOTFS_TYPE_GUID));

    return t.result();
}

fn testReadPartitionsUniqueGuidsDifferBetweenPartitions() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    t.expectTrue(@src(), !std.mem.eql(u8, &partitions[0].unique_guid, &partitions[1].unique_guid));
    return t.result();
}

fn testReadPartitionsTypeGuidsDifferBetweenEspAndRootfs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    t.expectTrue(@src(), !std.mem.eql(u8, &partitions[0].type_guid, &partitions[1].type_guid));
    return t.result();
}

fn testReadPartitionsLastLbaExceedsFirstLbaForEachPartition() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    for (partitions[0..count]) |p| {
        t.expectTrue(@src(), p.last_lba > p.first_lba);
    }
    return t.result();
}

fn testReadPartitionsNameIsNulTerminatedWithinBounds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    for (partitions[0..count]) |p| {
        const has_nul = std.mem.indexOfScalar(u8, &p.name, 0) != null;
        t.expectTrue(@src(), has_nul);
    }
    return t.result();
}

fn testReadPartitionsRootfsNameMatchesLabel() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    const name_len = std.mem.indexOfScalar(u8, &partitions[1].name, 0) orelse partitions[1].name.len;
    t.expectTrue(@src(), std.mem.eql(u8, partitions[1].name[0..name_len], "rootfs"));
    return t.result();
}

fn testReadPartitionsRepeatedCallsProduceIdenticalResults() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var first: [16]abi.GptPartition = undefined;
    var second: [16]abi.GptPartition = undefined;
    var count1: u32 = 0;
    var count2: u32 = 0;

    t.expectEqual(@src(), main.readPartitions(dev, &first, first.len, &count1), 0);
    t.expectEqual(@src(), main.readPartitions(dev, &second, second.len, &count2), 0);
    t.expectEqual(@src(), count1, count2);

    var i: u32 = 0;
    while (i < count1) : (i += 1) {
        t.expectTrue(@src(), std.mem.eql(u8, std.mem.asBytes(&first[i]), std.mem.asBytes(&second[i])));
    }
    return t.result();
}

fn testReadPartitionsWithMaxExactlyEqualToDiskCountReturnsAllNoTruncation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [2]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectEqual(@src(), count, 2);
    return t.result();
}

fn testReadPartitionsFirstEntryContentStableAcrossDifferentMaxPartitions() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var small: [1]abi.GptPartition = undefined;
    var large: [16]abi.GptPartition = undefined;
    var count_small: u32 = 0;
    var count_large: u32 = 0;

    t.expectEqual(@src(), main.readPartitions(dev, &small, small.len, &count_small), 0);
    t.expectEqual(@src(), main.readPartitions(dev, &large, large.len, &count_large), 0);
    t.expectEqual(@src(), count_small, 1);

    t.expectTrue(@src(), std.mem.eql(u8, std.mem.asBytes(&small[0]), std.mem.asBytes(&large[0])));
    return t.result();
}

fn testReadPartitionsDoesNotWriteBeyondRequestedMax() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [3]abi.GptPartition = undefined;
    const sentinel: abi.GptPartition = .{ .first_lba = 0xDEADBEEF, .last_lba = 0xDEADBEEF };
    partitions[2] = sentinel;

    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, 2, &count), 0);
    t.expectEqual(@src(), count, 2);

    // Index 2 is beyond `max_partitions == 2` and must be untouched.
    t.expectEqual(@src(), partitions[2].first_lba, sentinel.first_lba);
    t.expectEqual(@src(), partitions[2].last_lba, sentinel.last_lba);
    return t.result();
}

fn testReadPartitionsEntriesHaveNonZeroTypeGuid() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    const zero_guid = [_]u8{0} ** 16;
    for (partitions[0..count]) |p| {
        t.expectTrue(@src(), !std.mem.eql(u8, &p.type_guid, &zero_guid));
    }
    return t.result();
}

fn testReadPartitionsAllLbasWithinDiskBounds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    for (partitions[0..count]) |p| {
        t.expectTrue(@src(), p.last_lba < DISK_TOTAL_SECTORS);
    }
    return t.result();
}

fn testReadPartitionsRootfsIsContiguousAfterEsp() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    t.expectEqual(@src(), partitions[0].last_lba, ESP_LAST_LBA);
    t.expectEqual(@src(), partitions[1].first_lba, partitions[0].last_lba + 1);
    return t.result();
}

fn testReadPartitionsMaxPartitionsLargerThanTableSizeStillWorks() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    // sgdisk's default partition table has room for far more than 2
    // entries (usually 128) -- requesting far more than either the real
    // partition count or the table capacity must not overrun anything.
    var partitions: [1000]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectEqual(@src(), count, 2);
    return t.result();
}

fn testReadPartitionsCalledConsecutivelyManyTimesRemainsStable() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var i: u32 = 0;
    while (i < 5) : (i += 1) {
        var partitions: [16]abi.GptPartition = undefined;
        var count: u32 = 0;
        t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
        t.expectEqual(@src(), count, 2);
        t.expectEqual(@src(), partitions[0].first_lba, ESP_FIRST_LBA);
    }
    return t.result();
}

fn testReadPartitionsWithMaxPartitionsOneReturnsOnlyEsp() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [1]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectEqual(@src(), count, 1);
    t.expectEqual(@src(), partitions[0].first_lba, ESP_FIRST_LBA);
    t.expectTrue(@src(), std.mem.eql(u8, &partitions[0].type_guid, &ESP_TYPE_GUID));
    return t.result();
}

fn testReadPartitionsEspSizeInSectorsMatchesExpected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 1);

    const size_sectors = partitions[0].last_lba - partitions[0].first_lba + 1;
    t.expectEqual(@src(), size_sectors, ESP_LAST_LBA - ESP_FIRST_LBA + 1);
    return t.result();
}

fn testReadPartitionsRootfsSizeInSectorsMatchesExpected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const dev = openTestDevice();
    t.expectTrue(@src(), dev != null);

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    t.expectEqual(@src(), main.readPartitions(dev, &partitions, partitions.len, &count), 0);
    t.expectTrue(@src(), count >= 2);

    const size_sectors = partitions[1].last_lba - partitions[1].first_lba + 1;
    t.expectEqual(@src(), size_sectors, ROOTFS_LAST_LBA - ROOTFS_FIRST_LBA + 1);
    return t.result();
}

comptime {
    abi.kernelTest("gpt_read_partitions_finds_esp", &testReadPartitionsFindsEsp);
    abi.kernelTest("gpt_read_partitions_rejects_bad_args", &testReadPartitionsRejectsBadArgs);
    abi.kernelTest("gpt_read_partitions_respects_max_partitions", &testReadPartitionsRespectsMaxPartitions);

    abi.kernelTest("gpt_read_partitions_rejects_null_dev_with_valid_max", &testReadPartitionsRejectsNullDevWithValidMax);
    abi.kernelTest("gpt_read_partitions_rejects_zero_max_with_valid_dev", &testReadPartitionsRejectsZeroMaxWithValidDev);
    abi.kernelTest("gpt_read_partitions_rejects_both_null_dev_and_zero_max", &testReadPartitionsRejectsBothNullDevAndZeroMax);
    abi.kernelTest("gpt_read_partitions_count_out_is_zeroed_even_on_early_einval", &testReadPartitionsCountOutIsZeroedEvenOnEarlyEinval);
    abi.kernelTest("gpt_read_partitions_finds_rootfs_partition", &testReadPartitionsFindsRootfsPartition);
    abi.kernelTest("gpt_read_partitions_count_is_exactly_two_with_ample_capacity", &testReadPartitionsCountIsExactlyTwoWithAmpleCapacity);
    abi.kernelTest("gpt_read_partitions_order_matches_table_index", &testReadPartitionsOrderMatchesTableIndex);
    abi.kernelTest("gpt_read_partitions_unique_guids_differ_between_partitions", &testReadPartitionsUniqueGuidsDifferBetweenPartitions);
    abi.kernelTest("gpt_read_partitions_type_guids_differ_between_esp_and_rootfs", &testReadPartitionsTypeGuidsDifferBetweenEspAndRootfs);
    abi.kernelTest("gpt_read_partitions_last_lba_exceeds_first_lba_for_each_partition", &testReadPartitionsLastLbaExceedsFirstLbaForEachPartition);
    abi.kernelTest("gpt_read_partitions_name_is_nul_terminated_within_bounds", &testReadPartitionsNameIsNulTerminatedWithinBounds);
    abi.kernelTest("gpt_read_partitions_rootfs_name_matches_label", &testReadPartitionsRootfsNameMatchesLabel);
    abi.kernelTest("gpt_read_partitions_repeated_calls_produce_identical_results", &testReadPartitionsRepeatedCallsProduceIdenticalResults);
    abi.kernelTest("gpt_read_partitions_with_max_exactly_equal_to_disk_count_returns_all_no_truncation", &testReadPartitionsWithMaxExactlyEqualToDiskCountReturnsAllNoTruncation);
    abi.kernelTest("gpt_read_partitions_first_entry_content_stable_across_different_max_partitions", &testReadPartitionsFirstEntryContentStableAcrossDifferentMaxPartitions);
    abi.kernelTest("gpt_read_partitions_does_not_write_beyond_requested_max", &testReadPartitionsDoesNotWriteBeyondRequestedMax);
    abi.kernelTest("gpt_read_partitions_entries_have_non_zero_type_guid", &testReadPartitionsEntriesHaveNonZeroTypeGuid);
    abi.kernelTest("gpt_read_partitions_all_lbas_within_disk_bounds", &testReadPartitionsAllLbasWithinDiskBounds);
    abi.kernelTest("gpt_read_partitions_rootfs_is_contiguous_after_esp", &testReadPartitionsRootfsIsContiguousAfterEsp);
    abi.kernelTest("gpt_read_partitions_max_partitions_larger_than_table_size_still_works", &testReadPartitionsMaxPartitionsLargerThanTableSizeStillWorks);
    abi.kernelTest("gpt_read_partitions_called_consecutively_many_times_remains_stable", &testReadPartitionsCalledConsecutivelyManyTimesRemainsStable);
    abi.kernelTest("gpt_read_partitions_with_max_partitions_one_returns_only_esp", &testReadPartitionsWithMaxPartitionsOneReturnsOnlyEsp);
    abi.kernelTest("gpt_read_partitions_esp_size_in_sectors_matches_expected", &testReadPartitionsEspSizeInSectorsMatchesExpected);
    abi.kernelTest("gpt_read_partitions_rootfs_size_in_sectors_matches_expected", &testReadPartitionsRootfsSizeInSectorsMatchesExpected);
}
