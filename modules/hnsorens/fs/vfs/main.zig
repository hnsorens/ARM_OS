//! Whole-tree path resolver, exporting `Vfs` (category "vfs"). Owns the
//! entire find-device -> init -> block-device -> GPT -> ext2 mount
//! sequence at boot (`main`, run once as this module's entry point,
//! mirrors `things_to_add/ext2.c`'s `fs_start`), then serves path-string
//! operations by walking one `Ext2.lookup` call per component.
//!
//! `things_to_add/vfs.c` was a much rougher sketch than the other C
//! references -- it called undeclared globals (`FILESYSTEM`, `ROOT`,
//! `PATH`, `VFS_ENTRY_POOL`, ...) and functions with signatures that
//! don't match `ext2.c`'s real ones, so it wouldn't have compiled as-is.
//! This is a clean design serving the same purpose (path string ->
//! filesystem operation) rather than a line-by-line port.
//!
//! Path walking never special-cases "." or ".." -- every ext2 directory
//! (root included, and any directory `hnsorens.fs.ext2`'s `dir_create`
//! makes) already contains real `.`/`..` entries, so `Ext2.lookup`
//! resolves them the same as any other component, exactly like a real
//! Unix VFS treats them as ordinary directory entries rather than
//! path-language syntax.
//!
//! Symlinks are resolved during the walk itself (`walkFromDepth`):
//! whenever a path component's lookup lands on an `EXT2_FT_SYMLINK`,
//! its target is read back and walked in turn -- as an absolute path
//! from root if the target starts with `/`, otherwise relative to the
//! symlink's *own* containing directory (not the caller's current
//! directory, which this OS has no notion of -- there's no per-process
//! cwd here, every path is root-relative). Each hop increments a depth
//! counter capped at `MAX_SYMLINK_DEPTH`, so a symlink cycle fails with
//! `ELOOP` instead of recursing forever.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

pub const virtiobus_if = abi.importInterface(abi.VirtioBus);
pub const blkdevice_if = abi.importInterface(abi.BlkDevice);
pub const gpt_if = abi.importInterface(abi.Gpt);
pub const ext2_if = abi.importInterface(abi.Ext2);
pub const serial_if = abi.importInterface(abi.Serial);

const virtio_device_id = abi.declareConfigInt("virtio_device_id", u32, 2);
/// -1 (default) auto-selects the first GPT partition whose type is
/// "Linux filesystem" (0FC63DAF-8483-4772-8E79-3D69D8477DE4); a
/// non-negative value pins a specific partition index instead, for disk
/// layouts where that heuristic picks the wrong one.
const root_partition_index = abi.declareConfigInt("root_partition_index", i32, -1);

const LINUX_FS_TYPE_GUID: [16]u8 = .{ 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };
const MAX_PARTITIONS: usize = 32;
const MAX_PATH_LEN: usize = 256;
const MAX_COMPONENT_LEN: usize = 255;
/// Bounds both the number of symlink hops one `resolve` can take and the
/// longest symlink target this module will read back (matches common
/// Unix `MAXSYMLINKS`; long enough for anything reasonable, short enough
/// that a cycle can't spin for long before hitting `ELOOP`).
pub const MAX_SYMLINK_DEPTH: u32 = 40;

var g_fs: ?*anyopaque = null;
var g_mounted: bool = false;

fn mountRoot() bool {
    if (g_mounted) return true;

    const base = virtiobus_if.find_device(virtio_device_id.*);
    if (base == 0) return false;
    if (virtiobus_if.init_device(base) != 0) return false;

    var dev: ?*anyopaque = null;
    if (blkdevice_if.create(base, &dev) != 0) return false;

    var partitions: [MAX_PARTITIONS]abi.GptPartition = undefined;
    var count: u32 = 0;
    if (gpt_if.read_partitions(dev, &partitions, MAX_PARTITIONS, &count) != 0) return false;

    var chosen: ?usize = null;
    if (root_partition_index.* >= 0 and @as(u32, @intCast(root_partition_index.*)) < count) {
        chosen = @intCast(root_partition_index.*);
    } else {
        var i: u32 = 0;
        while (i < count) : (i += 1) {
            if (std.mem.eql(u8, &partitions[i].type_guid, &LINUX_FS_TYPE_GUID)) {
                chosen = i;
                break;
            }
        }
    }
    const idx = chosen orelse return false;

    if (ext2_if.mount(dev, partitions[idx].first_lba, partitions[idx].last_lba, &g_fs) != 0) return false;

    g_mounted = true;
    return true;
}

/// Symlink-target buffer size -- generous for a path, and matches
/// `splitParent`'s `MAX_PATH_LEN` so a symlink can point anywhere a
/// literal path argument could reach.
const MAX_SYMLINK_TARGET_LEN: usize = MAX_PATH_LEN;

fn walkFromDepth(start_inode: u32, start_type: u8, path: []const u8, inode_out: *u32, file_type_out: *u8, depth: u32) c_int {
    if (depth > MAX_SYMLINK_DEPTH) return abi.ELOOP;

    var current_inode = start_inode;
    var current_type = start_type;

    var p = path;
    if (p.len > 0 and p[0] == '/') {
        current_inode = ext2_if.root_inode(g_fs);
        current_type = abi.EXT2_FT_DIR;
        while (p.len > 0 and p[0] == '/') p = p[1..];
    }

    var it = std.mem.splitScalar(u8, p, '/');
    while (it.next()) |component| {
        if (component.len == 0) continue;
        if (component.len > MAX_COMPONENT_LEN) return abi.EINVAL;
        if (current_type != abi.EXT2_FT_DIR) return abi.ENOTDIR;

        var name_buf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
        @memcpy(name_buf[0..component.len], component);
        name_buf[component.len] = 0;

        var next_inode: u32 = 0;
        var next_type: u8 = 0;
        const rc = ext2_if.lookup(g_fs, current_inode, &name_buf, &next_inode, &next_type);
        if (rc != 0) return rc;

        if (next_type == abi.EXT2_FT_SYMLINK) {
            var target_buf: [MAX_SYMLINK_TARGET_LEN]u8 = undefined;
            var target_len: u64 = 0;
            const lrc = ext2_if.symlink_read(g_fs, next_inode, &target_buf, MAX_SYMLINK_TARGET_LEN, &target_len);
            if (lrc != 0) return lrc;

            // Relative targets resolve against the symlink's own
            // containing directory (`current_inode`, before this
            // lookup), not the caller's -- this OS has no per-process
            // cwd, every path argument is already root-relative.
            const wrc = walkFromDepth(current_inode, current_type, target_buf[0..target_len], &next_inode, &next_type, depth + 1);
            if (wrc != 0) return wrc;
        }

        current_inode = next_inode;
        current_type = next_type;
    }

    inode_out.* = current_inode;
    file_type_out.* = current_type;
    return 0;
}

fn walkTo(path: [*:0]const u8, inode_out: *u32, file_type_out: *u8) c_int {
    if (!mountRoot()) return abi.EIO;
    return walkFromDepth(ext2_if.root_inode(g_fs), abi.EXT2_FT_DIR, std.mem.sliceTo(path, 0), inode_out, file_type_out, 0);
}

/// Resolves `path`'s *parent* normally (following symlinks along the
/// way) but looks up the final component itself without following it --
/// for operations that must act on a symlink rather than its target
/// (`remove`, `lstat`, `readlink`), matching POSIX `unlink`/`lstat`/
/// `readlink` semantics.
fn resolveLeafNoFollow(path: [*:0]const u8, inode_out: *u32, file_type_out: *u8) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(path, &parent_inode, &leaf);
    if (rc != 0) return rc;
    return ext2_if.lookup(g_fs, parent_inode, &leaf, inode_out, file_type_out);
}

/// Splits `path` into its parent directory's inode and its final
/// component (for create/mkdir/remove, which all operate as
/// `(parent_inode, leaf_name)` at the `Ext2` layer).
fn splitParent(path: [*:0]const u8, parent_out: *u32, leaf_out: *[MAX_COMPONENT_LEN + 1:0]u8) c_int {
    if (!mountRoot()) return abi.EIO;

    var p = std.mem.sliceTo(path, 0);
    while (p.len > 0 and p[0] == '/') p = p[1..];
    if (p.len == 0) return abi.EINVAL;

    var last_slash: ?usize = null;
    for (p, 0..) |c, i| {
        if (c == '/') last_slash = i;
    }

    var parent_path_buf: [MAX_PATH_LEN:0]u8 = undefined;
    var leaf: []const u8 = undefined;

    if (last_slash) |ls| {
        const parent_part = p[0..ls];
        leaf = p[ls + 1 ..];
        if (parent_part.len >= MAX_PATH_LEN) return abi.EINVAL;
        @memcpy(parent_path_buf[0..parent_part.len], parent_part);
        parent_path_buf[parent_part.len] = 0;
    } else {
        leaf = p;
        parent_path_buf[0] = 0;
    }

    if (leaf.len == 0 or leaf.len > MAX_COMPONENT_LEN) return abi.EINVAL;

    var parent_inode: u32 = 0;
    var parent_type: u8 = 0;
    const rc = walkTo(&parent_path_buf, &parent_inode, &parent_type);
    if (rc != 0) return rc;
    if (parent_type != abi.EXT2_FT_DIR) return abi.ENOTDIR;

    parent_out.* = parent_inode;
    @memcpy(leaf_out[0..leaf.len], leaf);
    leaf_out[leaf.len] = 0;
    return 0;
}

pub fn resolve(path: [*:0]const u8, inode_out: *u32, file_type_out: *u8) callconv(.c) c_int {
    return walkTo(path, inode_out, file_type_out);
}

pub fn read(path: [*:0]const u8, offset: u64, buf: ?*anyopaque, count: u64, bytes_read_out: *u64) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = walkTo(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    if (file_type != abi.EXT2_FT_REG_FILE) return abi.EISDIR;
    return ext2_if.file_read(g_fs, inode_num, offset, buf, count, bytes_read_out);
}

pub fn write(path: [*:0]const u8, offset: u64, buf: ?*const anyopaque, count: u64, bytes_written_out: *u64) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = walkTo(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    if (file_type != abi.EXT2_FT_REG_FILE) return abi.EISDIR;
    return ext2_if.file_write(g_fs, inode_num, offset, buf, count, bytes_written_out);
}

pub fn create(path: [*:0]const u8, mode: u16) callconv(.c) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(path, &parent_inode, &leaf);
    if (rc != 0) return rc;

    var new_inode: u32 = 0;
    return ext2_if.file_create(g_fs, parent_inode, &leaf, mode, &new_inode);
}

pub fn mkdir(path: [*:0]const u8, mode: u16) callconv(.c) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(path, &parent_inode, &leaf);
    if (rc != 0) return rc;

    var new_inode: u32 = 0;
    return ext2_if.dir_create(g_fs, parent_inode, &leaf, mode, &new_inode);
}

pub fn remove(path: [*:0]const u8) callconv(.c) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(path, &parent_inode, &leaf);
    if (rc != 0) return rc;

    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const lrc = ext2_if.lookup(g_fs, parent_inode, &leaf, &inode_num, &file_type);
    if (lrc != 0) return lrc;

    if (file_type == abi.EXT2_FT_DIR) return ext2_if.dir_delete(g_fs, parent_inode, &leaf);
    return ext2_if.file_delete(g_fs, parent_inode, &leaf);
}

pub fn listDir(path: [*:0]const u8, index: u32, entry_out: *abi.Ext2DirEntry) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = walkTo(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    if (file_type != abi.EXT2_FT_DIR) return abi.ENOTDIR;
    return ext2_if.dir_read(g_fs, inode_num, index, entry_out);
}

pub fn rename(old_path: [*:0]const u8, new_path: [*:0]const u8) callconv(.c) c_int {
    var old_parent: u32 = 0;
    var old_leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    var rc = splitParent(old_path, &old_parent, &old_leaf);
    if (rc != 0) return rc;

    var new_parent: u32 = 0;
    var new_leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    rc = splitParent(new_path, &new_parent, &new_leaf);
    if (rc != 0) return rc;

    return ext2_if.rename(g_fs, old_parent, &old_leaf, new_parent, &new_leaf);
}

pub fn stat(path: [*:0]const u8, out: *abi.Ext2Stat) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = walkTo(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    return ext2_if.stat(g_fs, inode_num, out);
}

pub fn lstat(path: [*:0]const u8, out: *abi.Ext2Stat) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = resolveLeafNoFollow(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    return ext2_if.stat(g_fs, inode_num, out);
}

pub fn symlink(target: [*:0]const u8, link_path: [*:0]const u8) callconv(.c) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(link_path, &parent_inode, &leaf);
    if (rc != 0) return rc;

    var new_inode: u32 = 0;
    return ext2_if.symlink_create(g_fs, parent_inode, &leaf, target, &new_inode);
}

pub fn readlink(path: [*:0]const u8, buf: ?*anyopaque, buf_len: u64, len_out: *u64) callconv(.c) c_int {
    var inode_num: u32 = 0;
    var file_type: u8 = 0;
    const rc = resolveLeafNoFollow(path, &inode_num, &file_type);
    if (rc != 0) return rc;
    if (file_type != abi.EXT2_FT_SYMLINK) return abi.EINVAL;

    const bytes: [*]u8 = @ptrCast(buf orelse return abi.EINVAL);
    return ext2_if.symlink_read(g_fs, inode_num, bytes, buf_len, len_out);
}

pub fn mknod(path: [*:0]const u8, mode: u16, file_type: u8, dev: u32) callconv(.c) c_int {
    var parent_inode: u32 = 0;
    var leaf: [MAX_COMPONENT_LEN + 1:0]u8 = undefined;
    const rc = splitParent(path, &parent_inode, &leaf);
    if (rc != 0) return rc;

    var new_inode: u32 = 0;
    return ext2_if.mknod(g_fs, parent_inode, &leaf, mode, file_type, dev, &new_inode);
}

pub fn main(boot_info: *anyopaque) void {
    _ = boot_info;
    _ = mountRoot();
}

comptime {
    abi.exportInterface("vfs", abi.Vfs, .{
        .resolve = resolve,
        .read = read,
        .write = write,
        .create = create,
        .mkdir = mkdir,
        .remove = remove,
        .list_dir = listDir,
        .rename = rename,
        .stat = stat,
        .lstat = lstat,
        .symlink = symlink,
        .readlink = readlink,
        .mknod = mknod,
    });
}

comptime {
    _ = @import("test.zig");
}
