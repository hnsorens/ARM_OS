//! Tests for the ext2 driver in `main.zig`, split into their own file
//! (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! Mounts the real ext2 partition the root `build.zig` formats and seeds
//! with `tools/fixtures/hello.txt` (see its ext2-image-building steps),
//! not a mock -- same approach as `virtio_bus`/`virtio_blk`/`gpt`'s tests.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");
const main = @import("main.zig");

const serial_if = main.serial_if;

const VIRTIO_BLK_DEVICE_ID: u32 = 2;

// Linux filesystem GUID (0FC63DAF-8483-4772-8E79-3D69D8477DE4), as its
// mixed-endian on-disk byte encoding -- identifies partition 2 (see the
// root build.zig's `sgdisk_part`).
const LINUX_FS_TYPE_GUID: [16]u8 = .{ 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };

const HELLO_CONTENT = "Hello, ARM_OS filesystem!\n";

// Cached, not re-opened/re-mounted per test: `virtiobus_if.init_device`
// performs a full VirtIO device reset, and `blkdevice_if.create` is
// idempotent per mmio_base (see its doc comment) -- it won't redo queue
// setup/DRIVER_OK on a second call for an already-created device, so
// resetting the device again after that would leave it silently dead
// while every test still believed it was live. `Ext2.mount` also has a
// small fixed mount-table (`MAX_FS`), which every test remounting would
// exhaust well before all of this module's tests had run.
var s_test_fs: ?*anyopaque = null;
var s_test_dev: ?*anyopaque = null;
var s_test_part_lba: u64 = 0;

fn mountTestFs() ?*anyopaque {
    if (s_test_fs) |fs| return fs;

    const base = main.virtiobus_if.find_device(VIRTIO_BLK_DEVICE_ID);
    if (base == 0) return null;
    if (main.virtiobus_if.init_device(base) != 0) return null;

    var dev: ?*anyopaque = null;
    if (main.blkdevice_if.create(base, &dev) != 0) return null;

    var partitions: [16]abi.GptPartition = undefined;
    var count: u32 = 0;
    if (main.gpt_if.read_partitions(dev, &partitions, partitions.len, &count) != 0) return null;

    var i: u32 = 0;
    while (i < count) : (i += 1) {
        if (std.mem.eql(u8, &partitions[i].type_guid, &LINUX_FS_TYPE_GUID)) {
            var fs: ?*anyopaque = null;
            if (main.mount(dev, partitions[i].first_lba, partitions[i].last_lba, &fs) != 0) return null;
            s_test_fs = fs;
            s_test_dev = dev;
            s_test_part_lba = partitions[i].first_lba;
            return fs;
        }
    }
    return null;
}


fn testMountFindsRootAndHello() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    t.expectEqual(@src(), main.rootInode(fs), abi.EXT2_ROOT_INO);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "hello.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode_num, &st), 0);
    t.expectEqual(@src(), st.size, HELLO_CONTENT.len);

    return t.result();
}

fn testFileReadReturnsFixtureContent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "hello.txt", &inode_num, &file_type), 0);

    var buf: [64]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, inode_num, 0, &buf, buf.len, &bytes_read), 0);
    t.expectEqual(@src(), bytes_read, HELLO_CONTENT.len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..bytes_read], HELLO_CONTENT));

    return t.result();
}

fn testLookupMissingReturnsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "does_not_exist", &inode_num, &file_type), abi.ENOENT);

    return t.result();
}

fn testFileLifecycleCreateWriteReadDelete() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var new_inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "created_by_test.txt", 0o644, &new_inode), 0);
    t.expectTrue(@src(), new_inode != 0);

    const payload = "round trip";
    var bytes_written: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, new_inode, 0, payload, payload.len, &bytes_written), 0);
    t.expectEqual(@src(), bytes_written, payload.len);

    var buf: [32]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, new_inode, 0, &buf, buf.len, &bytes_read), 0);
    t.expectEqual(@src(), bytes_read, payload.len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..bytes_read], payload));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "created_by_test.txt"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "created_by_test.txt", &inode_num, &file_type), abi.ENOENT);

    return t.result();
}

fn testDirLifecycleCreateListDelete() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var dir_inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "testdir", 0o755, &dir_inode), 0);
    t.expectTrue(@src(), dir_inode != 0);

    // A fresh directory has no entries once '.'/'..' are excluded (see
    // main.zig's doc comment on why dir_read skips them).
    var entry: abi.Ext2DirEntry = undefined;
    t.expectEqual(@src(), main.dirRead(fs, dir_inode, 0, &entry), abi.ENOENT);

    var child_inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, dir_inode, "child.txt", 0o644, &child_inode), 0);

    t.expectEqual(@src(), main.dirRead(fs, dir_inode, 0, &entry), 0);
    t.expectEqual(@src(), entry.inode, child_inode);
    t.expectTrue(@src(), std.mem.eql(u8, std.mem.sliceTo(entry.name[0..], 0), "child.txt"));

    // A non-empty directory can't be removed.
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "testdir"), abi.ENOTEMPTY);

    t.expectEqual(@src(), main.fileDelete(fs, dir_inode, "child.txt"), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "testdir"), 0);

    return t.result();
}

fn testRenameMovesEntry() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_a: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "rename_src.txt", 0o644, &inode_a), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rename_src.txt", abi.EXT2_ROOT_INO, "rename_dst.txt"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "rename_src.txt", &inode_num, &file_type), abi.ENOENT);
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "rename_dst.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), inode_num, inode_a);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rename_dst.txt"), 0);

    return t.result();
}

fn testTruncateGrowsAndShrinks() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_num: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "truncate_test.txt", 0o644, &inode_num), 0);

    t.expectEqual(@src(), main.fileTruncate(fs, inode_num, 4096), 0);
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode_num, &st), 0);
    t.expectEqual(@src(), st.size, 4096);

    var buf: [16]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, inode_num, 4000, &buf, 16, &bytes_read), 0);
    t.expectEqual(@src(), bytes_read, 16);
    for (buf) |b| t.expectEqual(@src(), b, 0);

    t.expectEqual(@src(), main.fileTruncate(fs, inode_num, 10), 0);
    t.expectEqual(@src(), main.stat(fs, inode_num, &st), 0);
    t.expectEqual(@src(), st.size, 10);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "truncate_test.txt"), 0);

    return t.result();
}

// A name of exactly EXT2_NAME_LEN+1 bytes -- one over the limit every
// name-accepting call must reject with ENAMETOOLONG.
const NAME_TOO_LONG: [abi.EXT2_NAME_LEN + 1:0]u8 = [_:0]u8{'a'} ** (abi.EXT2_NAME_LEN + 1);

// A symlink target longer than FAST_SYMLINK_MAX_LEN (60 bytes), forcing
// the "slow" (data-block-backed) symlink path.
const LONG_TARGET: [80:0]u8 = ("0123456789" ** 8).*;

// --- Symlinks ----------------------------------------------------------------

fn testSymlinkFastCreateAndRead() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_fast1", "hello.txt", &link_inode), 0);
    t.expectTrue(@src(), link_inode != 0);

    var buf: [64]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, link_inode, &buf, buf.len, &len), 0);
    t.expectEqual(@src(), len, "hello.txt".len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..len], "hello.txt"));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_fast1"), 0);
    return t.result();
}

fn testSymlinkSlowCreateAndRead() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_slow1", &LONG_TARGET, &link_inode), 0);
    t.expectTrue(@src(), link_inode != 0);

    var buf: [128]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, link_inode, &buf, buf.len, &len), 0);
    t.expectEqual(@src(), len, LONG_TARGET.len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..len], &LONG_TARGET));

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, link_inode, &st), 0);
    t.expectEqual(@src(), st.size, LONG_TARGET.len);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_slow1"), 0);
    return t.result();
}

fn testSymlinkDanglingTargetAllowed() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_dangling", "/does/not/exist", &link_inode), 0);

    var buf: [32]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, link_inode, &buf, buf.len, &len), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..len], "/does/not/exist"));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_dangling"), 0);
    return t.result();
}

fn testSymlinkReadTruncatesToBufferLen() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_trunc", &LONG_TARGET, &link_inode), 0);

    var buf: [10]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, link_inode, &buf, buf.len, &len), 0);
    t.expectEqual(@src(), len, 10);
    t.expectTrue(@src(), std.mem.eql(u8, &buf, LONG_TARGET[0..10]));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_trunc"), 0);
    return t.result();
}

fn testSymlinkReadRejectsNonSymlink() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "hello.txt", &inode_num, &file_type), 0);

    var buf: [8]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, inode_num, &buf, buf.len, &len), abi.EINVAL);

    return t.result();
}

fn testSymlinkCreateRejectsDuplicateName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_dup", "a", &link_inode), 0);
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_dup", "b", &link_inode), abi.EEXIST);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_dup"), 0);
    return t.result();
}

fn testSymlinkCreateRejectsEmptyName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "", "target", &link_inode), abi.EINVAL);
    return t.result();
}

fn testSymlinkCreateRejectsDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, ".", "target", &link_inode), abi.EINVAL);
    return t.result();
}

fn testSymlinkCreateRejectsDotDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "..", "target", &link_inode), abi.EINVAL);
    return t.result();
}

fn testSymlinkCreateRejectsNameTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, &NAME_TOO_LONG, "target", &link_inode), abi.ENAMETOOLONG);
    return t.result();
}

fn testSymlinkStatModeAndSize() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_stat", "hello.txt", &link_inode), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, link_inode, &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFLNK);
    t.expectEqual(@src(), st.size, "hello.txt".len);

    var lookup_inode: u32 = 0;
    var lookup_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "sym_stat", &lookup_inode, &lookup_type), 0);
    t.expectEqual(@src(), lookup_type, abi.EXT2_FT_SYMLINK);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_stat"), 0);
    return t.result();
}

fn testSymlinkFastDeleteRemovesEntry() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_del_fast", "x", &link_inode), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_del_fast"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "sym_del_fast", &inode_num, &file_type), abi.ENOENT);
    return t.result();
}

fn testSymlinkSlowDeleteRemovesEntry() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_del_slow", &LONG_TARGET, &link_inode), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_del_slow"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "sym_del_slow", &inode_num, &file_type), abi.ENOENT);
    return t.result();
}

// --- Device / FIFO / socket nodes ---------------------------------------------

fn testMknodCharDeviceStatRdevRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_chr1", 0o600, abi.EXT2_FT_CHRDEV, 0x0500, &inode), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFCHR);
    t.expectEqual(@src(), st.rdev, 0x0500);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_chr1"), 0);
    return t.result();
}

fn testMknodBlockDeviceStatRdevRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_blk1", 0o600, abi.EXT2_FT_BLKDEV, 0x0801, &inode), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFBLK);
    t.expectEqual(@src(), st.rdev, 0x0801);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_blk1"), 0);
    return t.result();
}

fn testMknodFifoHasZeroRdev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_fifo1", 0o600, abi.EXT2_FT_FIFO, 0xDEAD, &inode), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFIFO);
    t.expectEqual(@src(), st.rdev, 0);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_fifo1"), 0);
    return t.result();
}

fn testMknodSocketHasZeroRdev() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_sock1", 0o600, abi.EXT2_FT_SOCK, 0xBEEF, &inode), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFSOCK);
    t.expectEqual(@src(), st.rdev, 0);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_sock1"), 0);
    return t.result();
}

fn testMknodRejectsRegularFileType() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_bad1", 0o600, abi.EXT2_FT_REG_FILE, 0, &inode), abi.EINVAL);
    return t.result();
}

fn testMknodRejectsDirFileType() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_bad2", 0o600, abi.EXT2_FT_DIR, 0, &inode), abi.EINVAL);
    return t.result();
}

fn testMknodRejectsSymlinkFileType() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_bad3", 0o600, abi.EXT2_FT_SYMLINK, 0, &inode), abi.EINVAL);
    return t.result();
}

fn testMknodRejectsDuplicateName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_dup", 0o600, abi.EXT2_FT_FIFO, 0, &inode), 0);
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_dup", 0o600, abi.EXT2_FT_FIFO, 0, &inode), abi.EEXIST);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_dup"), 0);
    return t.result();
}

fn testMknodRejectsEmptyName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "", 0o600, abi.EXT2_FT_FIFO, 0, &inode), abi.EINVAL);
    return t.result();
}

fn testMknodRejectsNameTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, &NAME_TOO_LONG, 0o600, abi.EXT2_FT_FIFO, 0, &inode), abi.ENAMETOOLONG);
    return t.result();
}

fn testMknodDeleteRemovesEntry() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_del", 0o600, abi.EXT2_FT_FIFO, 0, &inode), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_del"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "dev_del", &inode_num, &file_type), abi.ENOENT);
    return t.result();
}

fn testMknodLookupReportsCorrectFileType() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_type_check", 0o600, abi.EXT2_FT_BLKDEV, 1, &inode), 0);

    var lookup_inode: u32 = 0;
    var lookup_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "dev_type_check", &lookup_inode, &lookup_type), 0);
    t.expectEqual(@src(), lookup_type, abi.EXT2_FT_BLKDEV);
    t.expectEqual(@src(), lookup_inode, inode);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_type_check"), 0);
    return t.result();
}

// --- Mode guards on file_read/file_write/file_truncate ------------------------

fn testFileReadRejectsDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var buf: [8]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, abi.EXT2_ROOT_INO, 0, &buf, buf.len, &n), abi.EISDIR);
    return t.result();
}

fn testFileWriteRejectsDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    const payload = "x";
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, abi.EXT2_ROOT_INO, 0, payload, payload.len, &n), abi.EISDIR);
    return t.result();
}

fn testFileTruncateRejectsDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    t.expectEqual(@src(), main.fileTruncate(fs, abi.EXT2_ROOT_INO, 100), abi.EISDIR);
    return t.result();
}

fn testFileReadRejectsSymlink() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_no_read", "x", &link_inode), 0);

    var buf: [8]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, link_inode, 0, &buf, buf.len, &n), abi.EINVAL);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_no_read"), 0);
    return t.result();
}

fn testFileWriteRejectsSymlink() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_no_write", "x", &link_inode), 0);

    const payload = "y";
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, link_inode, 0, payload, payload.len, &n), abi.EINVAL);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_no_write"), 0);
    return t.result();
}

fn testFileTruncateRejectsSymlink() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "sym_no_trunc", "x", &link_inode), 0);
    t.expectEqual(@src(), main.fileTruncate(fs, link_inode, 5), abi.EINVAL);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "sym_no_trunc"), 0);
    return t.result();
}

fn testFileReadRejectsDeviceNode() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var dev_inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_no_read", 0o600, abi.EXT2_FT_CHRDEV, 1, &dev_inode), 0);

    var buf: [8]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileRead(fs, dev_inode, 0, &buf, buf.len, &n), abi.EINVAL);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_no_read"), 0);
    return t.result();
}

fn testFileWriteRejectsDeviceNode() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var dev_inode: u32 = 0;
    t.expectEqual(@src(), main.mknod(fs, abi.EXT2_ROOT_INO, "dev_no_write", 0o600, abi.EXT2_FT_CHRDEV, 1, &dev_inode), 0);

    const payload = "z";
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, dev_inode, 0, payload, payload.len, &n), abi.EINVAL);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dev_no_write"), 0);
    return t.result();
}

// --- file_delete/dir_delete generalized type handling -------------------------

fn testFileDeleteRejectsDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var dir_inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "fd_reject_dir", 0o755, &dir_inode), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "fd_reject_dir"), abi.EISDIR);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "fd_reject_dir"), 0);
    return t.result();
}

fn testDirDeleteRejectsRegularFile() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "dd_reject_file.txt", 0o644, &inode), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "dd_reject_file.txt"), abi.ENOTDIR);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "dd_reject_file.txt"), 0);
    return t.result();
}

fn testDirDeleteRejectsNonexistent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "does_not_exist_dd"), abi.ENOENT);
    return t.result();
}

// --- Name validation across create-style operations ---------------------------

fn testFileCreateRejectsEmptyName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "", 0o644, &inode), abi.EINVAL);
    return t.result();
}

fn testFileCreateRejectsDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, ".", 0o644, &inode), abi.EINVAL);
    return t.result();
}

fn testFileCreateRejectsDotDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "..", 0o644, &inode), abi.EINVAL);
    return t.result();
}

fn testFileCreateRejectsNameTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, &NAME_TOO_LONG, 0o644, &inode), abi.ENAMETOOLONG);
    return t.result();
}

fn testDirCreateRejectsEmptyName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "", 0o755, &inode), abi.EINVAL);
    return t.result();
}

fn testDirCreateRejectsDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, ".", 0o755, &inode), abi.EINVAL);
    return t.result();
}

fn testDirCreateRejectsDotDotName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "..", 0o755, &inode), abi.EINVAL);
    return t.result();
}

fn testDirCreateRejectsNameTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    var inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, &NAME_TOO_LONG, 0o755, &inode), abi.ENAMETOOLONG);
    return t.result();
}

// --- Rename hardening ----------------------------------------------------------

fn testRenameSamePathIsNoOp() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "rn_noop.txt", 0o644, &inode), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_noop.txt", abi.EXT2_ROOT_INO, "rn_noop.txt"), 0);

    var lookup_inode: u32 = 0;
    var lookup_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "rn_noop.txt", &lookup_inode, &lookup_type), 0);
    t.expectEqual(@src(), lookup_inode, inode);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rn_noop.txt"), 0);
    return t.result();
}

fn testRenameNonexistentSourceReturnsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_no_such_src", abi.EXT2_ROOT_INO, "rn_dst"), abi.ENOENT);
    return t.result();
}

fn testRenameExistingDestinationReturnsEexist() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var a: u32 = 0;
    var b: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "rn_exist_a.txt", 0o644, &a), 0);
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "rn_exist_b.txt", 0o644, &b), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_exist_a.txt", abi.EXT2_ROOT_INO, "rn_exist_b.txt"), abi.EEXIST);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rn_exist_a.txt"), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rn_exist_b.txt"), 0);
    return t.result();
}

fn testRenameRejectsInvalidOldName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "", abi.EXT2_ROOT_INO, "rn_x"), abi.EINVAL);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, ".", abi.EXT2_ROOT_INO, "rn_x"), abi.EINVAL);
    return t.result();
}

fn testRenameRejectsInvalidNewName() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "rn_badnew.txt", 0o644, &inode), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_badnew.txt", abi.EXT2_ROOT_INO, ".."), abi.EINVAL);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rn_badnew.txt"), 0);
    return t.result();
}

fn testRenameDirectoryAcrossParentsUpdatesDotDot() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var parent_a: u32 = 0;
    var parent_b: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "rn_parent_a", 0o755, &parent_a), 0);
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "rn_parent_b", 0o755, &parent_b), 0);

    var moved: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, parent_a, "moved", 0o755, &moved), 0);

    t.expectEqual(@src(), main.rename(fs, parent_a, "moved", parent_b, "moved"), 0);

    // The moved directory's own ".." must now point at parent_b.
    var dotdot_inode: u32 = 0;
    var dotdot_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, moved, "..", &dotdot_inode, &dotdot_type), 0);
    t.expectEqual(@src(), dotdot_inode, parent_b);

    // parent_a no longer has it, parent_b does.
    var lookup_inode: u32 = 0;
    var lookup_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, parent_a, "moved", &lookup_inode, &lookup_type), abi.ENOENT);
    t.expectEqual(@src(), main.lookup(fs, parent_b, "moved", &lookup_inode, &lookup_type), 0);

    t.expectEqual(@src(), main.dirDelete(fs, parent_b, "moved"), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "rn_parent_a"), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "rn_parent_b"), 0);
    return t.result();
}

fn testRenameDirectoryIntoOwnSubtreeRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var outer: u32 = 0;
    var inner: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "rn_outer", 0o755, &outer), 0);
    t.expectEqual(@src(), main.dirCreate(fs, outer, "rn_inner", 0o755, &inner), 0);

    // Moving "rn_outer" to become a child of its own descendant "rn_inner"
    // would detach it from the tree entirely -- must be refused.
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_outer", inner, "rn_outer_moved"), abi.EINVAL);

    t.expectEqual(@src(), main.dirDelete(fs, outer, "rn_inner"), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "rn_outer"), 0);
    return t.result();
}

fn testRenameDirectoryIntoItselfRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var dir_inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "rn_self", 0o755, &dir_inode), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_self", dir_inode, "rn_self_inside"), abi.EINVAL);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "rn_self"), 0);
    return t.result();
}

fn testRenameSymlinkPreservesTarget() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var link_inode: u32 = 0;
    t.expectEqual(@src(), main.symlinkCreate(fs, abi.EXT2_ROOT_INO, "rn_sym_src", "hello.txt", &link_inode), 0);
    t.expectEqual(@src(), main.rename(fs, abi.EXT2_ROOT_INO, "rn_sym_src", abi.EXT2_ROOT_INO, "rn_sym_dst"), 0);

    var buf: [16]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.symlinkRead(fs, link_inode, &buf, buf.len, &len), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..len], "hello.txt"));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "rn_sym_dst"), 0);
    return t.result();
}

// --- Data integrity / block indirection ----------------------------------------

fn testFileGrowPastEofZerosGapOnWrite() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "gap_write.txt", 0o644, &inode), 0);

    // Write 4 bytes at offset 0, then 4 more bytes far past that -- the
    // gap in between must read back as zero.
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, inode, 0, "AAAA", 4, &n), 0);
    t.expectEqual(@src(), main.fileWrite(fs, inode, 2000, "BBBB", 4, &n), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.size, 2004);

    var gap_buf: [32]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 100, &gap_buf, 32, &n), 0);
    for (gap_buf) |b| t.expectEqual(@src(), b, 0);

    var tail_buf: [4]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 2000, &tail_buf, 4, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, &tail_buf, "BBBB"));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "gap_write.txt"), 0);
    return t.result();
}

fn testFileTruncateGrowReusedBlockReadsZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    // Write real, non-zero data across several blocks, then delete the
    // file -- its blocks return to the free pool with that data still on
    // disk (ext2 never scrubs freed blocks). A second file that grows
    // into those same blocks via truncate must not expose it.
    var victim: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "reuse_victim.txt", 0o644, &victim), 0);
    var n: u64 = 0;
    var pattern: [3000]u8 = undefined;
    for (&pattern, 0..) |*b, i| b.* = @truncate(0xAA ^ i);
    t.expectEqual(@src(), main.fileWrite(fs, victim, 0, &pattern, pattern.len, &n), 0);
    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "reuse_victim.txt"), 0);

    var grower: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "reuse_grower.txt", 0o644, &grower), 0);
    t.expectEqual(@src(), main.fileTruncate(fs, grower, 3000), 0);

    var readback: [3000]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, grower, 0, &readback, readback.len, &n), 0);
    t.expectEqual(@src(), n, 3000);
    for (readback) |b| t.expectEqual(@src(), b, 0);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "reuse_grower.txt"), 0);
    return t.result();
}

fn testFileWriteReadAcrossDirectBlockBoundary() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "cross_direct.txt", 0o644, &inode), 0);

    var pattern: [2500]u8 = undefined;
    for (&pattern, 0..) |*b, i| b.* = @truncate(i);
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, inode, 0, &pattern, pattern.len, &n), 0);
    t.expectEqual(@src(), n, pattern.len);

    var readback: [2500]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 0, &readback, readback.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, &readback, &pattern));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "cross_direct.txt"), 0);
    return t.result();
}

fn testFileWriteReadAcrossIndirectBoundary() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "cross_indirect.txt", 0o644, &inode), 0);

    // 12 direct blocks * 1024 bytes = 12288; write spans well past that
    // into the single-indirect range.
    const write_len = 15000;
    var pattern: [write_len]u8 = undefined;
    for (&pattern, 0..) |*b, i| b.* = @truncate(i ^ 0x5A);
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, inode, 0, &pattern, write_len, &n), 0);
    t.expectEqual(@src(), n, write_len);

    var readback: [write_len]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 0, &readback, write_len, &n), 0);
    t.expectEqual(@src(), n, write_len);
    t.expectTrue(@src(), std.mem.eql(u8, &readback, &pattern));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "cross_indirect.txt"), 0);
    return t.result();
}

fn testFileReadPastEofReturnsZeroBytes() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "past_eof.txt", 0o644, &inode), 0);
    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, inode, 0, "hi", 2, &n), 0);

    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 100, &buf, buf.len, &n), 0);
    t.expectEqual(@src(), n, 0);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "past_eof.txt"), 0);
    return t.result();
}

fn testFileWriteAppendsAcrossMultipleCalls() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "append.txt", 0o644, &inode), 0);

    var n: u64 = 0;
    t.expectEqual(@src(), main.fileWrite(fs, inode, 0, "abc", 3, &n), 0);
    t.expectEqual(@src(), main.fileWrite(fs, inode, 3, "def", 3, &n), 0);
    t.expectEqual(@src(), main.fileWrite(fs, inode, 6, "ghi", 3, &n), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.size, 9);

    var buf: [9]u8 = undefined;
    t.expectEqual(@src(), main.fileRead(fs, inode, 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, &buf, "abcdefghi"));

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "append.txt"), 0);
    return t.result();
}

// --- Directory stress / bitmap reuse --------------------------------------------

fn testDirectoryManyEntriesListedCorrectly() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var stress_dir: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "stress_dir", 0o755, &stress_dir), 0);

    const n_entries = 90;
    var name_buf: [16:0]u8 = undefined;
    var i: u32 = 0;
    while (i < n_entries) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "f{d}", .{i}) catch unreachable;
        var inode: u32 = 0;
        t.expectEqual(@src(), main.fileCreate(fs, stress_dir, name, 0o644, &inode), 0);
    }

    var count: u32 = 0;
    var entry: abi.Ext2DirEntry = undefined;
    while (main.dirRead(fs, stress_dir, count, &entry) == 0) : (count += 1) {}
    t.expectEqual(@src(), count, n_entries);

    i = 0;
    while (i < n_entries) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "f{d}", .{i}) catch unreachable;
        t.expectEqual(@src(), main.fileDelete(fs, stress_dir, name), 0);
    }
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "stress_dir"), 0);
    return t.result();
}

fn testCreateDeleteCycleStaysConsistent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var i: u32 = 0;
    while (i < 8) : (i += 1) {
        var inode: u32 = 0;
        t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "cycle.txt", 0o644, &inode), 0);
        t.expectTrue(@src(), inode != 0);

        var n: u64 = 0;
        t.expectEqual(@src(), main.fileWrite(fs, inode, 0, "cycle", 5, &n), 0);

        var lookup_inode: u32 = 0;
        var lookup_type: u8 = 0;
        t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "cycle.txt", &lookup_inode, &lookup_type), 0);
        t.expectEqual(@src(), lookup_inode, inode);

        t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "cycle.txt"), 0);
    }
    return t.result();
}

fn testManySmallFilesCreatedThenAllRemoved() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    const n_files = 20;
    var name_buf: [16:0]u8 = undefined;
    var i: u32 = 0;
    while (i < n_files) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "small{d}.txt", .{i}) catch unreachable;
        var inode: u32 = 0;
        t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, name, 0o644, &inode), 0);
    }

    i = 0;
    while (i < n_files) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "small{d}.txt", .{i}) catch unreachable;
        var lookup_inode: u32 = 0;
        var lookup_type: u8 = 0;
        t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, name, &lookup_inode, &lookup_type), 0);
    }

    i = 0;
    while (i < n_files) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "small{d}.txt", .{i}) catch unreachable;
        t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, name), 0);
    }

    i = 0;
    while (i < n_files) : (i += 1) {
        const name = std.fmt.bufPrintZ(&name_buf, "small{d}.txt", .{i}) catch unreachable;
        var lookup_inode: u32 = 0;
        var lookup_type: u8 = 0;
        t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, name, &lookup_inode, &lookup_type), abi.ENOENT);
    }
    return t.result();
}

// --- Stat field correctness -----------------------------------------------------

fn testStatRegularFileLinksCountIsOne() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "links_reg.txt", 0o644, &inode), 0);
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.links_count, 1);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "links_reg.txt"), 0);
    return t.result();
}

fn testStatNewDirLinksCountIsTwo() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "links_dir", 0o755, &inode), 0);
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.links_count, 2);

    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "links_dir"), 0);
    return t.result();
}

fn testStatDirWithSubdirLinksCountIsThree() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var parent: u32 = 0;
    var child: u32 = 0;
    t.expectEqual(@src(), main.dirCreate(fs, abi.EXT2_ROOT_INO, "links_parent", 0o755, &parent), 0);
    t.expectEqual(@src(), main.dirCreate(fs, parent, "links_child", 0o755, &child), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, parent, &st), 0);
    t.expectEqual(@src(), st.links_count, 3);

    t.expectEqual(@src(), main.dirDelete(fs, parent, "links_child"), 0);
    t.expectEqual(@src(), main.dirDelete(fs, abi.EXT2_ROOT_INO, "links_parent"), 0);
    return t.result();
}

fn testStatModeBitsPreserveRequestedPermissions() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "mode_check.txt", 0o741, &inode), 0);
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.mode & 0xFFF, 0o741);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFREG);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "mode_check.txt"), 0);
    return t.result();
}

fn testFileCreateThenImmediateStatSizeIsZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "fresh.txt", 0o644, &inode), 0);
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, inode, &st), 0);
    t.expectEqual(@src(), st.size, 0);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "fresh.txt"), 0);
    return t.result();
}

// --- Misc lookup / edge cases ----------------------------------------------------

fn testLookupInNonDirectoryParentFails() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var file_inode: u32 = 0;
    t.expectEqual(@src(), main.fileCreate(fs, abi.EXT2_ROOT_INO, "not_a_dir.txt", 0o644, &file_inode), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, file_inode, "anything", &inode_num, &file_type), abi.ENOENT);

    t.expectEqual(@src(), main.fileDelete(fs, abi.EXT2_ROOT_INO, "not_a_dir.txt"), 0);
    return t.result();
}

fn testRootInodeIsAlwaysEXT2ROOTINO() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);
    t.expectEqual(@src(), main.rootInode(fs), abi.EXT2_ROOT_INO);
    t.expectEqual(@src(), main.rootInode(null), abi.EXT2_ROOT_INO);
    return t.result();
}

fn testStatOnInvalidInodeReturnsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat(fs, 0xFFFFFF, &st), abi.ENOENT);
    return t.result();
}

fn testLookupEmptyNameReturnsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const fs = mountTestFs();
    t.expectTrue(@src(), fs != null);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.lookup(fs, abi.EXT2_ROOT_INO, "", &inode_num, &file_type), abi.ENOENT);
    return t.result();
}

comptime {
    abi.kernelTest("ext2_mount_finds_root_and_hello", &testMountFindsRootAndHello);
    abi.kernelTest("ext2_file_read_returns_fixture_content", &testFileReadReturnsFixtureContent);
    abi.kernelTest("ext2_lookup_missing_returns_enoent", &testLookupMissingReturnsEnoent);
    abi.kernelTest("ext2_file_lifecycle_create_write_read_delete", &testFileLifecycleCreateWriteReadDelete);
    abi.kernelTest("ext2_dir_lifecycle_create_list_delete", &testDirLifecycleCreateListDelete);
    abi.kernelTest("ext2_rename_moves_entry", &testRenameMovesEntry);
    abi.kernelTest("ext2_truncate_grows_and_shrinks", &testTruncateGrowsAndShrinks);

    abi.kernelTest("ext2_symlink_fast_create_and_read", &testSymlinkFastCreateAndRead);
    abi.kernelTest("ext2_symlink_slow_create_and_read", &testSymlinkSlowCreateAndRead);
    abi.kernelTest("ext2_symlink_dangling_target_allowed", &testSymlinkDanglingTargetAllowed);
    abi.kernelTest("ext2_symlink_read_truncates_to_buffer_len", &testSymlinkReadTruncatesToBufferLen);
    abi.kernelTest("ext2_symlink_read_rejects_non_symlink", &testSymlinkReadRejectsNonSymlink);
    abi.kernelTest("ext2_symlink_create_rejects_duplicate_name", &testSymlinkCreateRejectsDuplicateName);
    abi.kernelTest("ext2_symlink_create_rejects_empty_name", &testSymlinkCreateRejectsEmptyName);
    abi.kernelTest("ext2_symlink_create_rejects_dot_name", &testSymlinkCreateRejectsDotName);
    abi.kernelTest("ext2_symlink_create_rejects_dotdot_name", &testSymlinkCreateRejectsDotDotName);
    abi.kernelTest("ext2_symlink_create_rejects_name_too_long", &testSymlinkCreateRejectsNameTooLong);
    abi.kernelTest("ext2_symlink_stat_mode_and_size", &testSymlinkStatModeAndSize);
    abi.kernelTest("ext2_symlink_fast_delete_removes_entry", &testSymlinkFastDeleteRemovesEntry);
    abi.kernelTest("ext2_symlink_slow_delete_removes_entry", &testSymlinkSlowDeleteRemovesEntry);

    abi.kernelTest("ext2_mknod_chardev_stat_rdev_round_trip", &testMknodCharDeviceStatRdevRoundTrip);
    abi.kernelTest("ext2_mknod_blockdev_stat_rdev_round_trip", &testMknodBlockDeviceStatRdevRoundTrip);
    abi.kernelTest("ext2_mknod_fifo_has_zero_rdev", &testMknodFifoHasZeroRdev);
    abi.kernelTest("ext2_mknod_socket_has_zero_rdev", &testMknodSocketHasZeroRdev);
    abi.kernelTest("ext2_mknod_rejects_regular_file_type", &testMknodRejectsRegularFileType);
    abi.kernelTest("ext2_mknod_rejects_dir_file_type", &testMknodRejectsDirFileType);
    abi.kernelTest("ext2_mknod_rejects_symlink_file_type", &testMknodRejectsSymlinkFileType);
    abi.kernelTest("ext2_mknod_rejects_duplicate_name", &testMknodRejectsDuplicateName);
    abi.kernelTest("ext2_mknod_rejects_empty_name", &testMknodRejectsEmptyName);
    abi.kernelTest("ext2_mknod_rejects_name_too_long", &testMknodRejectsNameTooLong);
    abi.kernelTest("ext2_mknod_delete_removes_entry", &testMknodDeleteRemovesEntry);
    abi.kernelTest("ext2_mknod_lookup_reports_correct_file_type", &testMknodLookupReportsCorrectFileType);

    abi.kernelTest("ext2_file_read_rejects_directory", &testFileReadRejectsDirectory);
    abi.kernelTest("ext2_file_write_rejects_directory", &testFileWriteRejectsDirectory);
    abi.kernelTest("ext2_file_truncate_rejects_directory", &testFileTruncateRejectsDirectory);
    abi.kernelTest("ext2_file_read_rejects_symlink", &testFileReadRejectsSymlink);
    abi.kernelTest("ext2_file_write_rejects_symlink", &testFileWriteRejectsSymlink);
    abi.kernelTest("ext2_file_truncate_rejects_symlink", &testFileTruncateRejectsSymlink);
    abi.kernelTest("ext2_file_read_rejects_device_node", &testFileReadRejectsDeviceNode);
    abi.kernelTest("ext2_file_write_rejects_device_node", &testFileWriteRejectsDeviceNode);

    abi.kernelTest("ext2_file_delete_rejects_directory", &testFileDeleteRejectsDirectory);
    abi.kernelTest("ext2_dir_delete_rejects_regular_file", &testDirDeleteRejectsRegularFile);
    abi.kernelTest("ext2_dir_delete_rejects_nonexistent", &testDirDeleteRejectsNonexistent);

    abi.kernelTest("ext2_file_create_rejects_empty_name", &testFileCreateRejectsEmptyName);
    abi.kernelTest("ext2_file_create_rejects_dot_name", &testFileCreateRejectsDotName);
    abi.kernelTest("ext2_file_create_rejects_dotdot_name", &testFileCreateRejectsDotDotName);
    abi.kernelTest("ext2_file_create_rejects_name_too_long", &testFileCreateRejectsNameTooLong);
    abi.kernelTest("ext2_dir_create_rejects_empty_name", &testDirCreateRejectsEmptyName);
    abi.kernelTest("ext2_dir_create_rejects_dot_name", &testDirCreateRejectsDotName);
    abi.kernelTest("ext2_dir_create_rejects_dotdot_name", &testDirCreateRejectsDotDotName);
    abi.kernelTest("ext2_dir_create_rejects_name_too_long", &testDirCreateRejectsNameTooLong);

    abi.kernelTest("ext2_rename_same_path_is_noop", &testRenameSamePathIsNoOp);
    abi.kernelTest("ext2_rename_nonexistent_source_returns_enoent", &testRenameNonexistentSourceReturnsEnoent);
    abi.kernelTest("ext2_rename_existing_destination_returns_eexist", &testRenameExistingDestinationReturnsEexist);
    abi.kernelTest("ext2_rename_rejects_invalid_old_name", &testRenameRejectsInvalidOldName);
    abi.kernelTest("ext2_rename_rejects_invalid_new_name", &testRenameRejectsInvalidNewName);
    abi.kernelTest("ext2_rename_directory_across_parents_updates_dotdot", &testRenameDirectoryAcrossParentsUpdatesDotDot);
    abi.kernelTest("ext2_rename_directory_into_own_subtree_rejected", &testRenameDirectoryIntoOwnSubtreeRejected);
    abi.kernelTest("ext2_rename_directory_into_itself_rejected", &testRenameDirectoryIntoItselfRejected);
    abi.kernelTest("ext2_rename_symlink_preserves_target", &testRenameSymlinkPreservesTarget);

    abi.kernelTest("ext2_file_grow_past_eof_zeros_gap_on_write", &testFileGrowPastEofZerosGapOnWrite);
    abi.kernelTest("ext2_file_truncate_grow_reused_block_reads_zero", &testFileTruncateGrowReusedBlockReadsZero);
    abi.kernelTest("ext2_file_write_read_across_direct_block_boundary", &testFileWriteReadAcrossDirectBlockBoundary);
    abi.kernelTest("ext2_file_write_read_across_indirect_boundary", &testFileWriteReadAcrossIndirectBoundary);
    abi.kernelTest("ext2_file_read_past_eof_returns_zero_bytes", &testFileReadPastEofReturnsZeroBytes);
    abi.kernelTest("ext2_file_write_appends_across_multiple_calls", &testFileWriteAppendsAcrossMultipleCalls);

    abi.kernelTest("ext2_directory_many_entries_listed_correctly", &testDirectoryManyEntriesListedCorrectly);
    abi.kernelTest("ext2_create_delete_cycle_stays_consistent", &testCreateDeleteCycleStaysConsistent);
    abi.kernelTest("ext2_many_small_files_created_then_all_removed", &testManySmallFilesCreatedThenAllRemoved);

    abi.kernelTest("ext2_stat_regular_file_links_count_is_one", &testStatRegularFileLinksCountIsOne);
    abi.kernelTest("ext2_stat_new_dir_links_count_is_two", &testStatNewDirLinksCountIsTwo);
    abi.kernelTest("ext2_stat_dir_with_subdir_links_count_is_three", &testStatDirWithSubdirLinksCountIsThree);
    abi.kernelTest("ext2_stat_mode_bits_preserve_requested_permissions", &testStatModeBitsPreserveRequestedPermissions);
    abi.kernelTest("ext2_file_create_then_immediate_stat_size_is_zero", &testFileCreateThenImmediateStatSizeIsZero);

    abi.kernelTest("ext2_lookup_in_non_directory_parent_fails", &testLookupInNonDirectoryParentFails);
    abi.kernelTest("ext2_root_inode_is_always_ext2_root_ino", &testRootInodeIsAlwaysEXT2ROOTINO);
    abi.kernelTest("ext2_stat_on_invalid_inode_returns_enoent", &testStatOnInvalidInodeReturnsEnoent);
    abi.kernelTest("ext2_lookup_empty_name_returns_enoent", &testLookupEmptyNameReturnsEnoent);
}
