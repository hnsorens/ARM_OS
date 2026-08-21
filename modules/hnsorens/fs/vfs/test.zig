//! Tests for the VFS path resolver in `main.zig`, split into their own
//! file (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! `main`'s auto-mount already ran by the time these execute (the
//! bootloader runs a module's entry point before its `kernelTest`s --
//! see `shared/abi.zig`'s doc comment), but `walkTo`'s lazy `mountRoot()`
//! is a safety net regardless. Reads/writes the real ext2 partition the
//! root `build.zig` formats and seeds with `tools/fixtures/hello.txt`.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const HELLO_CONTENT = "Hello, ARM_OS filesystem!\n";

fn testResolveRootAndHello() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/", &inode_num, &file_type), 0);
    t.expectEqual(@src(), inode_num, abi.EXT2_ROOT_INO);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_DIR);

    t.expectEqual(@src(), main.resolve("/hello.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    t.expectEqual(@src(), main.resolve("/does_not_exist", &inode_num, &file_type), abi.ENOENT);

    return t.result();
}

fn testReadReturnsFixtureContent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var buf: [64]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.read("/hello.txt", 0, &buf, buf.len, &bytes_read), 0);
    t.expectEqual(@src(), bytes_read, HELLO_CONTENT.len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..bytes_read], HELLO_CONTENT));

    return t.result();
}

fn testReadRejectsDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var buf: [16]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.read("/", 0, &buf, buf.len, &bytes_read), abi.EISDIR);

    return t.result();
}

fn testMkdirCreateWriteReadNestedPath() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/vfs_test_dir", 0o755), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/vfs_test_dir", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_DIR);

    t.expectEqual(@src(), main.create("/vfs_test_dir/nested.txt", 0o644), 0);

    const payload = "nested payload";
    var bytes_written: u64 = 0;
    t.expectEqual(@src(), main.write("/vfs_test_dir/nested.txt", 0, payload, payload.len, &bytes_written), 0);
    t.expectEqual(@src(), bytes_written, payload.len);

    var buf: [32]u8 = undefined;
    var bytes_read: u64 = 0;
    t.expectEqual(@src(), main.read("/vfs_test_dir/nested.txt", 0, &buf, buf.len, &bytes_read), 0);
    t.expectEqual(@src(), bytes_read, payload.len);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..bytes_read], payload));

    var entry: abi.Ext2DirEntry = undefined;
    t.expectEqual(@src(), main.listDir("/vfs_test_dir", 0, &entry), 0);
    t.expectTrue(@src(), std.mem.eql(u8, std.mem.sliceTo(entry.name[0..], 0), "nested.txt"));

    // Non-empty directory can't be removed.
    t.expectEqual(@src(), main.remove("/vfs_test_dir"), abi.ENOTEMPTY);

    t.expectEqual(@src(), main.remove("/vfs_test_dir/nested.txt"), 0);
    t.expectEqual(@src(), main.remove("/vfs_test_dir"), 0);

    t.expectEqual(@src(), main.resolve("/vfs_test_dir", &inode_num, &file_type), abi.ENOENT);

    return t.result();
}

fn testDotAndDotDotResolveThroughRealDirents() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/./hello.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    t.expectEqual(@src(), main.mkdir("/vfs_dotdot_dir", 0o755), 0);
    t.expectEqual(@src(), main.resolve("/vfs_dotdot_dir/../hello.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    var root_inode: u32 = 0;
    t.expectEqual(@src(), main.resolve("/vfs_dotdot_dir/..", &root_inode, &file_type), 0);
    t.expectEqual(@src(), root_inode, abi.EXT2_ROOT_INO);

    t.expectEqual(@src(), main.remove("/vfs_dotdot_dir"), 0);

    return t.result();
}

fn testCreateRejectsMissingParent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.create("/no_such_dir/file.txt", 0o644), abi.ENOENT);
    return t.result();
}

// A path with one >255-byte component -- every path-accepting call must
// reject this the same way, whichever component it lands on.
const TOO_LONG_COMPONENT_PATH: [301:0]u8 = ("/" ++ "a" ** 300).*;

// --- Symlinks ------------------------------------------------------------------

fn testVfsSymlinkCreateAndResolveToFile() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("hello.txt", "/link_to_hello"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/link_to_hello", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    var buf: [64]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/link_to_hello", 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..n], HELLO_CONTENT));

    t.expectEqual(@src(), main.remove("/link_to_hello"), 0);
    return t.result();
}

fn testVfsSymlinkChainFollowedToFinalTarget() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("hello.txt", "/chain_c"), 0);
    t.expectEqual(@src(), main.symlink("/chain_c", "/chain_b"), 0);
    t.expectEqual(@src(), main.symlink("/chain_b", "/chain_a"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/chain_a", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    t.expectEqual(@src(), main.remove("/chain_a"), 0);
    t.expectEqual(@src(), main.remove("/chain_b"), 0);
    t.expectEqual(@src(), main.remove("/chain_c"), 0);
    return t.result();
}

fn testVfsSymlinkLoopDetected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("/loop_b", "/loop_a"), 0);
    t.expectEqual(@src(), main.symlink("/loop_a", "/loop_b"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/loop_a", &inode_num, &file_type), abi.ELOOP);

    t.expectEqual(@src(), main.remove("/loop_a"), 0);
    t.expectEqual(@src(), main.remove("/loop_b"), 0);
    return t.result();
}

fn testVfsResolveDanglingSymlinkFailsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("/no/such/thing", "/dangle_link"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/dangle_link", &inode_num, &file_type), abi.ENOENT);

    // But the link itself genuinely exists -- lstat (not following) finds it.
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.lstat("/dangle_link", &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFLNK);

    t.expectEqual(@src(), main.remove("/dangle_link"), 0);
    return t.result();
}

fn testVfsStatFollowsSymlinkLstatDoesNot() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("/hello.txt", "/link_distinct"), 0);

    var st_stat: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat("/link_distinct", &st_stat), 0);
    t.expectEqual(@src(), st_stat.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFREG);
    t.expectEqual(@src(), st_stat.size, HELLO_CONTENT.len);

    var st_lstat: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.lstat("/link_distinct", &st_lstat), 0);
    t.expectEqual(@src(), st_lstat.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFLNK);
    t.expectEqual(@src(), st_lstat.size, "/hello.txt".len);

    t.expectEqual(@src(), main.remove("/link_distinct"), 0);
    return t.result();
}

fn testVfsRemoveOnSymlinkRemovesLinkNotTarget() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("/hello.txt", "/rm_link"), 0);
    t.expectEqual(@src(), main.remove("/rm_link"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/rm_link", &inode_num, &file_type), abi.ENOENT);
    t.expectEqual(@src(), main.resolve("/hello.txt", &inode_num, &file_type), 0);
    return t.result();
}

fn testVfsSymlinkRelativeTargetResolvesFromContainingDir() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/rel_dir", 0o755), 0);
    t.expectEqual(@src(), main.create("/rel_dir/target.txt", 0o644), 0);
    var w: u64 = 0;
    t.expectEqual(@src(), main.write("/rel_dir/target.txt", 0, "hi", 2, &w), 0);
    t.expectEqual(@src(), main.symlink("target.txt", "/rel_dir/link_to_target"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/rel_dir/link_to_target", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    var buf: [8]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/rel_dir/link_to_target", 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..n], "hi"));

    t.expectEqual(@src(), main.remove("/rel_dir/link_to_target"), 0);
    t.expectEqual(@src(), main.remove("/rel_dir/target.txt"), 0);
    t.expectEqual(@src(), main.remove("/rel_dir"), 0);
    return t.result();
}

fn testVfsSymlinkAbsoluteTargetResolvesFromRoot() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/abs_dir", 0o755), 0);
    t.expectEqual(@src(), main.symlink("/hello.txt", "/abs_dir/link_abs"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/abs_dir/link_abs", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);

    t.expectEqual(@src(), main.remove("/abs_dir/link_abs"), 0);
    t.expectEqual(@src(), main.remove("/abs_dir"), 0);
    return t.result();
}

fn testVfsReadlinkReturnsTarget() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("some/target", "/rl_test"), 0);

    var buf: [32]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.readlink("/rl_test", &buf, buf.len, &len), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..len], "some/target"));

    t.expectEqual(@src(), main.remove("/rl_test"), 0);
    return t.result();
}

fn testVfsReadlinkRejectsNonSymlink() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var buf: [8]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.readlink("/hello.txt", &buf, buf.len, &len), abi.EINVAL);
    return t.result();
}

fn testVfsReadlinkBufferTooSmallTruncates() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("0123456789abcdef", "/rl_small"), 0);

    var buf: [4]u8 = undefined;
    var len: u64 = 0;
    t.expectEqual(@src(), main.readlink("/rl_small", &buf, buf.len, &len), 0);
    t.expectEqual(@src(), len, 4);
    t.expectTrue(@src(), std.mem.eql(u8, &buf, "0123"));

    t.expectEqual(@src(), main.remove("/rl_small"), 0);
    return t.result();
}

fn testVfsSymlinkToDirectoryFollowedForListing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/sym_target_dir", 0o755), 0);
    t.expectEqual(@src(), main.create("/sym_target_dir/inner.txt", 0o644), 0);
    t.expectEqual(@src(), main.symlink("/sym_target_dir", "/sym_to_dir"), 0);

    var entry: abi.Ext2DirEntry = undefined;
    t.expectEqual(@src(), main.listDir("/sym_to_dir", 0, &entry), 0);
    t.expectTrue(@src(), std.mem.eql(u8, std.mem.sliceTo(entry.name[0..], 0), "inner.txt"));

    t.expectEqual(@src(), main.remove("/sym_to_dir"), 0);
    t.expectEqual(@src(), main.remove("/sym_target_dir/inner.txt"), 0);
    t.expectEqual(@src(), main.remove("/sym_target_dir"), 0);
    return t.result();
}

// --- mknod via path --------------------------------------------------------------

fn testVfsMknodCharDeviceAndStat() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mknod("/vfs_dev_chr", 0o600, abi.EXT2_FT_CHRDEV, 0x0100), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat("/vfs_dev_chr", &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFCHR);
    t.expectEqual(@src(), st.rdev, 0x0100);

    t.expectEqual(@src(), main.remove("/vfs_dev_chr"), 0);
    return t.result();
}

fn testVfsMknodFifoAndStat() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mknod("/vfs_dev_fifo", 0o600, abi.EXT2_FT_FIFO, 0), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat("/vfs_dev_fifo", &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFIFO);

    t.expectEqual(@src(), main.remove("/vfs_dev_fifo"), 0);
    return t.result();
}

fn testVfsMknodRejectsInvalidType() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.mknod("/vfs_dev_bad", 0o600, abi.EXT2_FT_REG_FILE, 0), abi.EINVAL);
    return t.result();
}

fn testVfsMknodThenRemoveViaGenericRemove() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mknod("/mk_rm", 0o600, abi.EXT2_FT_FIFO, 0), 0);
    t.expectEqual(@src(), main.remove("/mk_rm"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/mk_rm", &inode_num, &file_type), abi.ENOENT);
    return t.result();
}

// --- rename via path ---------------------------------------------------------------

fn testVfsRenameMovesFileAcrossDirs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/rn_a", 0o755), 0);
    t.expectEqual(@src(), main.mkdir("/rn_b", 0o755), 0);
    t.expectEqual(@src(), main.create("/rn_a/f.txt", 0o644), 0);

    t.expectEqual(@src(), main.rename("/rn_a/f.txt", "/rn_b/f2.txt"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/rn_a/f.txt", &inode_num, &file_type), abi.ENOENT);
    t.expectEqual(@src(), main.resolve("/rn_b/f2.txt", &inode_num, &file_type), 0);

    t.expectEqual(@src(), main.remove("/rn_b/f2.txt"), 0);
    t.expectEqual(@src(), main.remove("/rn_a"), 0);
    t.expectEqual(@src(), main.remove("/rn_b"), 0);
    return t.result();
}

fn testVfsRenameNestedPaths() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/rn_nest", 0o755), 0);
    t.expectEqual(@src(), main.mkdir("/rn_nest/sub", 0o755), 0);
    t.expectEqual(@src(), main.create("/rn_nest/before.txt", 0o644), 0);

    t.expectEqual(@src(), main.rename("/rn_nest/before.txt", "/rn_nest/sub/after.txt"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/rn_nest/sub/after.txt", &inode_num, &file_type), 0);

    t.expectEqual(@src(), main.remove("/rn_nest/sub/after.txt"), 0);
    t.expectEqual(@src(), main.remove("/rn_nest/sub"), 0);
    t.expectEqual(@src(), main.remove("/rn_nest"), 0);
    return t.result();
}

fn testVfsRenameNonexistentSourceFails() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.rename("/no_such_source.txt", "/whatever.txt"), abi.ENOENT);
    return t.result();
}

fn testVfsRenameSamePathIsNoop() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/rn_noop_vfs.txt", 0o644), 0);
    t.expectEqual(@src(), main.rename("/rn_noop_vfs.txt", "/rn_noop_vfs.txt"), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/rn_noop_vfs.txt", &inode_num, &file_type), 0);

    t.expectEqual(@src(), main.remove("/rn_noop_vfs.txt"), 0);
    return t.result();
}

// --- Path parsing / structural edge cases --------------------------------------

fn testVfsResolveHandlesMultipleSlashes() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("//hello.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_REG_FILE);
    return t.result();
}

fn testVfsResolveTrailingSlashOnDir() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/trailing_dir", 0o755), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/trailing_dir/", &inode_num, &file_type), 0);
    t.expectEqual(@src(), file_type, abi.EXT2_FT_DIR);

    t.expectEqual(@src(), main.remove("/trailing_dir"), 0);
    return t.result();
}

fn testVfsResolveRejectsComponentTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve(&TOO_LONG_COMPONENT_PATH, &inode_num, &file_type), abi.EINVAL);
    return t.result();
}

fn testVfsCreateRejectsNameTooLong() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.create(&TOO_LONG_COMPONENT_PATH, 0o644), abi.EINVAL);
    return t.result();
}

fn testVfsDeepNestedPathResolution() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.mkdir("/deep1", 0o755), 0);
    t.expectEqual(@src(), main.mkdir("/deep1/deep2", 0o755), 0);
    t.expectEqual(@src(), main.mkdir("/deep1/deep2/deep3", 0o755), 0);
    t.expectEqual(@src(), main.create("/deep1/deep2/deep3/leaf.txt", 0o644), 0);

    var w: u64 = 0;
    t.expectEqual(@src(), main.write("/deep1/deep2/deep3/leaf.txt", 0, "deep", 4, &w), 0);

    var buf: [8]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/deep1/deep2/deep3/leaf.txt", 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..n], "deep"));

    t.expectEqual(@src(), main.remove("/deep1/deep2/deep3/leaf.txt"), 0);
    t.expectEqual(@src(), main.remove("/deep1/deep2/deep3"), 0);
    t.expectEqual(@src(), main.remove("/deep1/deep2"), 0);
    t.expectEqual(@src(), main.remove("/deep1"), 0);
    return t.result();
}

fn testVfsMkdirNestedRejectsMissingIntermediateParent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.mkdir("/no_such_parent_vfs/child", 0o755), abi.ENOENT);
    return t.result();
}

fn testVfsListDirOnFileReturnsEnotdir() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var entry: abi.Ext2DirEntry = undefined;
    t.expectEqual(@src(), main.listDir("/hello.txt", 0, &entry), abi.ENOTDIR);
    return t.result();
}

fn testVfsStatRootDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat("/", &st), 0);
    t.expectEqual(@src(), st.mode & abi.EXT2_S_IFMT, abi.EXT2_S_IFDIR);
    return t.result();
}

fn testVfsCreateThenStatMatchesSize() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/stat_size.txt", 0o644), 0);
    var w: u64 = 0;
    t.expectEqual(@src(), main.write("/stat_size.txt", 0, "12345", 5, &w), 0);

    var st: abi.Ext2Stat = undefined;
    t.expectEqual(@src(), main.stat("/stat_size.txt", &st), 0);
    t.expectEqual(@src(), st.size, 5);

    t.expectEqual(@src(), main.remove("/stat_size.txt"), 0);
    return t.result();
}

// --- General CRUD via path ---------------------------------------------------------

fn testVfsWriteAtOffsetPastCurrentSize() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/gap_vfs.txt", 0o644), 0);
    var w: u64 = 0;
    t.expectEqual(@src(), main.write("/gap_vfs.txt", 500, "end", 3, &w), 0);

    var buf: [10]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/gap_vfs.txt", 0, &buf, buf.len, &n), 0);
    for (buf) |b| t.expectEqual(@src(), b, 0);

    t.expectEqual(@src(), main.remove("/gap_vfs.txt"), 0);
    return t.result();
}

fn testVfsMultipleFilesInSameDirectory() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/multi_a.txt", 0o644), 0);
    t.expectEqual(@src(), main.create("/multi_b.txt", 0o644), 0);
    t.expectEqual(@src(), main.create("/multi_c.txt", 0o644), 0);

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    t.expectEqual(@src(), main.resolve("/multi_a.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), main.resolve("/multi_b.txt", &inode_num, &file_type), 0);
    t.expectEqual(@src(), main.resolve("/multi_c.txt", &inode_num, &file_type), 0);

    t.expectEqual(@src(), main.remove("/multi_a.txt"), 0);
    t.expectEqual(@src(), main.remove("/multi_b.txt"), 0);
    t.expectEqual(@src(), main.remove("/multi_c.txt"), 0);
    return t.result();
}

fn testVfsOverwriteExistingFileContent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/ovr.txt", 0o644), 0);
    var w: u64 = 0;
    t.expectEqual(@src(), main.write("/ovr.txt", 0, "AAAA", 4, &w), 0);
    t.expectEqual(@src(), main.write("/ovr.txt", 0, "BB", 2, &w), 0);

    var buf: [4]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/ovr.txt", 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, &buf, "BBAA"));

    t.expectEqual(@src(), main.remove("/ovr.txt"), 0);
    return t.result();
}

fn testVfsCreateRejectsDuplicatePath() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.create("/dup_vfs.txt", 0o644), 0);
    t.expectEqual(@src(), main.create("/dup_vfs.txt", 0o644), abi.EEXIST);

    t.expectEqual(@src(), main.remove("/dup_vfs.txt"), 0);
    return t.result();
}

fn testVfsRemoveNonexistentPathReturnsEnoent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.remove("/no_such_file_xyz"), abi.ENOENT);
    return t.result();
}

fn testVfsReadFollowsSymlinkToFileContent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.symlink("hello.txt", "/read_via_link"), 0);

    var buf: [64]u8 = undefined;
    var n: u64 = 0;
    t.expectEqual(@src(), main.read("/read_via_link", 0, &buf, buf.len, &n), 0);
    t.expectTrue(@src(), std.mem.eql(u8, buf[0..n], HELLO_CONTENT));

    t.expectEqual(@src(), main.remove("/read_via_link"), 0);
    return t.result();
}

comptime {
    abi.kernelTest("vfs_resolve_root_and_hello", &testResolveRootAndHello);
    abi.kernelTest("vfs_read_returns_fixture_content", &testReadReturnsFixtureContent);
    abi.kernelTest("vfs_read_rejects_directory", &testReadRejectsDirectory);
    abi.kernelTest("vfs_mkdir_create_write_read_nested_path", &testMkdirCreateWriteReadNestedPath);
    abi.kernelTest("vfs_dot_and_dotdot_resolve_through_real_dirents", &testDotAndDotDotResolveThroughRealDirents);
    abi.kernelTest("vfs_create_rejects_missing_parent", &testCreateRejectsMissingParent);

    abi.kernelTest("vfs_symlink_create_and_resolve_to_file", &testVfsSymlinkCreateAndResolveToFile);
    abi.kernelTest("vfs_symlink_chain_followed_to_final_target", &testVfsSymlinkChainFollowedToFinalTarget);
    abi.kernelTest("vfs_symlink_loop_detected", &testVfsSymlinkLoopDetected);
    abi.kernelTest("vfs_resolve_dangling_symlink_fails_enoent", &testVfsResolveDanglingSymlinkFailsEnoent);
    abi.kernelTest("vfs_stat_follows_symlink_lstat_does_not", &testVfsStatFollowsSymlinkLstatDoesNot);
    abi.kernelTest("vfs_remove_on_symlink_removes_link_not_target", &testVfsRemoveOnSymlinkRemovesLinkNotTarget);
    abi.kernelTest("vfs_symlink_relative_target_resolves_from_containing_dir", &testVfsSymlinkRelativeTargetResolvesFromContainingDir);
    abi.kernelTest("vfs_symlink_absolute_target_resolves_from_root", &testVfsSymlinkAbsoluteTargetResolvesFromRoot);
    abi.kernelTest("vfs_readlink_returns_target", &testVfsReadlinkReturnsTarget);
    abi.kernelTest("vfs_readlink_rejects_non_symlink", &testVfsReadlinkRejectsNonSymlink);
    abi.kernelTest("vfs_readlink_buffer_too_small_truncates", &testVfsReadlinkBufferTooSmallTruncates);
    abi.kernelTest("vfs_symlink_to_directory_followed_for_listing", &testVfsSymlinkToDirectoryFollowedForListing);
    abi.kernelTest("vfs_read_follows_symlink_to_file_content", &testVfsReadFollowsSymlinkToFileContent);

    abi.kernelTest("vfs_mknod_chardevice_and_stat", &testVfsMknodCharDeviceAndStat);
    abi.kernelTest("vfs_mknod_fifo_and_stat", &testVfsMknodFifoAndStat);
    abi.kernelTest("vfs_mknod_rejects_invalid_type", &testVfsMknodRejectsInvalidType);
    abi.kernelTest("vfs_mknod_then_remove_via_generic_remove", &testVfsMknodThenRemoveViaGenericRemove);

    abi.kernelTest("vfs_rename_moves_file_across_dirs", &testVfsRenameMovesFileAcrossDirs);
    abi.kernelTest("vfs_rename_nested_paths", &testVfsRenameNestedPaths);
    abi.kernelTest("vfs_rename_nonexistent_source_fails", &testVfsRenameNonexistentSourceFails);
    abi.kernelTest("vfs_rename_same_path_is_noop", &testVfsRenameSamePathIsNoop);

    abi.kernelTest("vfs_resolve_handles_multiple_slashes", &testVfsResolveHandlesMultipleSlashes);
    abi.kernelTest("vfs_resolve_trailing_slash_on_dir", &testVfsResolveTrailingSlashOnDir);
    abi.kernelTest("vfs_resolve_rejects_component_too_long", &testVfsResolveRejectsComponentTooLong);
    abi.kernelTest("vfs_create_rejects_name_too_long", &testVfsCreateRejectsNameTooLong);
    abi.kernelTest("vfs_deep_nested_path_resolution", &testVfsDeepNestedPathResolution);
    abi.kernelTest("vfs_mkdir_nested_rejects_missing_intermediate_parent", &testVfsMkdirNestedRejectsMissingIntermediateParent);
    abi.kernelTest("vfs_list_dir_on_file_returns_enotdir", &testVfsListDirOnFileReturnsEnotdir);
    abi.kernelTest("vfs_stat_root_directory", &testVfsStatRootDirectory);
    abi.kernelTest("vfs_create_then_stat_matches_size", &testVfsCreateThenStatMatchesSize);

    abi.kernelTest("vfs_write_at_offset_past_current_size", &testVfsWriteAtOffsetPastCurrentSize);
    abi.kernelTest("vfs_multiple_files_in_same_directory", &testVfsMultipleFilesInSameDirectory);
    abi.kernelTest("vfs_overwrite_existing_file_content", &testVfsOverwriteExistingFileContent);
    abi.kernelTest("vfs_create_rejects_duplicate_path", &testVfsCreateRejectsDuplicatePath);
    abi.kernelTest("vfs_remove_nonexistent_path_returns_enoent", &testVfsRemoveNonexistentPathReturnsEnoent);
}
