//! ext2 filesystem driver, exporting `Ext2` (category "ext2"). Ported
//! from `things_to_add/ext2.c`, reading/writing through an imported
//! `BlkDevice` instead of calling driver functions directly, and working
//! in terms of `(dir_inode, name)` rather than the original's
//! `file_descriptor_t` (multi-component path walking is the vfs module's
//! job).
//!
//! On-disk structures (superblock, block group descriptor, inode,
//! directory entry) are written directly from the ext2 spec, since the
//! C reference's `#include "ext2.h"` wasn't among the provided files.
//!
//! Real bugs in the C reference are fixed here, not carried over:
//!   - `ext2_dir_create` allocated a directory's first block but never
//!     wrote `.`/`..` into it (nor zeroed it), so any directory it
//!     created was unreadable garbage the moment something tried to list
//!     it. This port zeroes the block and writes real `.`/`..` entries.
//!   - `ext2_dir_delete`'s "is this directory empty" check counted *any*
//!     entry at all, which -- once `.`/`..` are actually present --
//!     would make it refuse to delete every directory. This port counts
//!     entries excluding `.`/`..`.
//!   - `read_block_pointers` treated an entirely-unallocated indirect (or
//!     double-indirect) block as a short read, which callers like
//!     `ext2_file_read` treated as end-of-file -- so a file with a hole
//!     spanning a whole indirect block would silently truncate instead of
//!     reading zeros past it. This port zero-fills those holes instead,
//!     matching how an individual sparse *data* block already worked.
//!   - Growing a file (`ext2_file_write` past EOF, or `ext2_file_truncate`)
//!     never zeroed the blocks it newly allocated -- ext2 doesn't
//!     guarantee free blocks are already zero, so a block a *previous*
//!     file had freed could still hold that file's old data, which the
//!     new owner would then expose as if it were legitimate zero-filled
//!     grown space. This port's `ensureBlocksAllocated` zeroes every
//!     block it hands out before linking it in.
//!
//! Beyond the C reference's scope, this also adds: symlinks (fast and
//! slow), device/FIFO/socket special files, triple-indirect blocks (the
//! C reference only went to double), an in-memory superblock/block-group-
//! descriptor-table cache (both are re-read from disk on essentially
//! every allocation otherwise), and a per-mount lock (see `shared/
//! spinlock.zig`) around every public entry point.
//!
//! No heap is used anywhere in this module: every scratch buffer (block
//! reads, bitmaps, dirent buffers) comes from `phys_mem` (backed by
//! `Pmm`) and is freed before returning, and block-pointer allocation
//! streams one block at a time rather than building a dynamically-sized
//! array, matching the "no general-purpose heap in the storage stack"
//! design used throughout (see `phys_mem.zig`).
//!
//! Every `pub fn` below (the `abi.Ext2` vtable) takes `fs.lock` and calls
//! into an `*Impl`/lowercase-named private worker that assumes it's
//! already held -- internal helpers only ever call each other or the
//! `*Impl` functions directly, never back through a public entry point,
//! so nothing here re-enters the (non-reentrant) lock.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");
const spinlock = @import("spinlock");

pub const blkdevice_if = abi.importInterface(abi.BlkDevice);
pub const pmm_if = abi.importInterface(abi.Pmm);
pub const serial_if = abi.importInterface(abi.Serial);

// Not used by any exported Ext2 function -- imported so this module's own
// tests can independently discover a real device and its ext2-formatted
// partition to mount (mirroring `gpt`'s test-only `VirtioBus` import).
pub const virtiobus_if = abi.importInterface(abi.VirtioBus);
pub const gpt_if = abi.importInterface(abi.Gpt);

pub const SECTOR_SIZE: u64 = 512;
const EXT2_SIGNATURE: u16 = 0xEF53;

// `Ext2Inode.mode`'s file-type bits (POSIX S_IFMT values) -- kept as
// module-local consts (matching this file's existing pattern of not
// reaching for shared constants for on-disk-format details) even though
// `abi_types.zig` now exposes the same values for external callers to
// interpret `Ext2Stat.mode`.
const EXT2_S_IFMT: u16 = 0xF000;
const EXT2_S_IFSOCK: u16 = 0xC000;
const EXT2_S_IFLNK: u16 = 0xA000;
const EXT2_S_IFREG: u16 = 0x8000;
const EXT2_S_IFBLK: u16 = 0x6000;
const EXT2_S_IFDIR: u16 = 0x4000;
const EXT2_S_IFCHR: u16 = 0x2000;
const EXT2_S_IFIFO: u16 = 0x1000;

/// Longest symlink target storable directly in `inode.block[]` (15 * 4
/// bytes) without allocating a data block -- ext2's "fast symlink".
const FAST_SYMLINK_MAX_LEN: usize = 15 * 4;

fn timeNow() u32 {
    // No RTC in this OS yet -- matches the C reference's `time()` stub,
    // which just echoed back whatever it was given (effectively always 0
    // for every call site here, which all passed a literal 0).
    return 0;
}

// --- On-disk structures (ext2 spec) -----------------------------------------

const Ext2Superblock = extern struct {
    inodes_count: u32,
    blocks_count: u32,
    r_blocks_count: u32,
    free_blocks_count: u32,
    free_inodes_count: u32,
    first_data_block: u32,
    log_block_size: u32,
    log_frag_size: u32,
    blocks_per_group: u32,
    frags_per_group: u32,
    inodes_per_group: u32,
    mtime: u32,
    wtime: u32,
    mnt_count: u16,
    max_mnt_count: u16,
    magic: u16,
    state: u16,
    errors: u16,
    minor_rev_level: u16,
    lastcheck: u32,
    checkinterval: u32,
    creator_os: u32,
    rev_level: u32,
    def_resuid: u16,
    def_resgid: u16,
    first_ino: u32,
    inode_size: u16,
    block_group_nr: u16,
    feature_compat: u32,
    feature_incompat: u32,
    feature_ro_compat: u32,
    uuid: [16]u8,
    volume_name: [16]u8,
    last_mounted: [64]u8,
    algo_bitmap: u32,
};

const Ext2BgDesc = extern struct {
    block_bitmap: u32,
    inode_bitmap: u32,
    inode_table: u32,
    free_blocks_count: u16,
    free_inodes_count: u16,
    used_dirs_count: u16,
    pad: u16,
    reserved: [12]u8,
};

const Ext2Inode = extern struct {
    mode: u16,
    uid: u16,
    size: u32,
    atime: u32,
    ctime: u32,
    mtime: u32,
    dtime: u32,
    gid: u16,
    links_count: u16,
    blocks: u32,
    flags: u32,
    osd1: u32,
    block: [15]u32,
    generation: u32,
    file_acl: u32,
    dir_acl: u32,
    faddr: u32,
    osd2: [12]u8,
};

const Ext2DirentOnDisk = extern struct {
    inode: u32,
    rec_len: u16,
    name_len: u8,
    file_type: u8,
};

comptime {
    if (@sizeOf(Ext2Inode) != 128) @compileError("Ext2Inode must be exactly 128 bytes");
    if (@sizeOf(Ext2BgDesc) != 32) @compileError("Ext2BgDesc must be exactly 32 bytes");
    if (@sizeOf(Ext2DirentOnDisk) != 8) @compileError("Ext2DirentOnDisk must be exactly 8 bytes");
}

// --- Mount table -------------------------------------------------------------

/// Block group descriptors this many groups and below are kept in an
/// in-memory cache (`Fs.bgdt_cache`), turning every `readBgDesc` (which
/// `readInode`/`writeInode`/every allocation call needs) into a plain
/// memory read instead of a disk round trip; groups beyond it (a disk far
/// larger than anything this OS targets today) fall back to reading from
/// disk every time, same as before caching existed. Deliberately modest
/// (64 groups * 32 bytes = 2KB per mounted `Fs`, comfortably covering
/// disks up to several GB with 1KB blocks): an earlier 1024-group version
/// of this cache ballooned each `Fs`'s static footprint enough to corrupt
/// unrelated bootloader/module memory once two filesystems were mounted
/// at once (this module's own test plus `hnsorens.fs.vfs`'s) -- the
/// module loader has no dynamic sizing for a module's data/bss, so this
/// stays small rather than "generously large."
const MAX_CACHED_GROUPS: usize = 64;

const Fs = struct {
    dev: ?*anyopaque = null,
    start_sector: u64 = 0,
    end_sector: u64 = 0,
    block_size: u32 = 1024,
    blocks_per_group: u32 = 0,
    inodes_per_group: u32 = 0,
    first_data_block: u32 = 0,
    total_blocks: u32 = 0,
    total_inodes: u32 = 0,
    groups_count: u32 = 0,
    bgdt_block: u32 = 0,
    inode_size: u32 = 128,
    lock: spinlock.SpinLock = .{},

    /// The raw 1024-byte (2-sector) superblock region, kept in memory
    /// after mount and written straight through on every change -- not
    /// just the fields modeled by `Ext2Superblock` (which stops at
    /// `algo_bitmap`, well short of 1024 bytes), so a write-back never
    /// clobbers the reserved/journal/etc. bytes past it that this driver
    /// doesn't otherwise touch.
    sb_cache: [1024]u8 = [_]u8{0} ** 1024,
    bgdt_cache: [MAX_CACHED_GROUPS]Ext2BgDesc = [_]Ext2BgDesc{std.mem.zeroes(Ext2BgDesc)} ** MAX_CACHED_GROUPS,
};

fn sbView(fs: *Fs) *Ext2Superblock {
    return @ptrCast(@alignCast(&fs.sb_cache));
}

const MAX_FS: usize = 2;
var s_filesystems: [MAX_FS]Fs = [_]Fs{.{}} ** MAX_FS;
var s_fs_count: usize = 0;

fn asFs(ptr: ?*anyopaque) ?*Fs {
    return @ptrCast(@alignCast(ptr));
}

// --- Block/bg-desc/inode I/O ------------------------------------------------

const BlockBuf = struct { virt: u64, phys: u64 };

fn readBlock(fs: *Fs, block_num: u32) ?BlockBuf {
    if (block_num == 0 or block_num >= fs.total_blocks) return null;
    var virt: u64 = undefined;
    var phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, fs.block_size, &virt, &phys) != 0) return null;

    const sectors_per_block = fs.block_size / SECTOR_SIZE;
    const lba = fs.start_sector + @as(u64, block_num) * sectors_per_block;
    const buf: [*]u8 = @ptrFromInt(virt);
    if (blkdevice_if.read_sectors(fs.dev, lba, buf, sectors_per_block) != 0) {
        phys_mem.free(pmm_if, phys);
        return null;
    }
    return .{ .virt = virt, .phys = phys };
}

fn freeBlockBuf(buf: BlockBuf) void {
    phys_mem.free(pmm_if, buf.phys);
}

fn writeBlock(fs: *Fs, block_num: u32, virt: u64) bool {
    if (block_num == 0 or block_num >= fs.total_blocks) return false;
    const sectors_per_block = fs.block_size / SECTOR_SIZE;
    const lba = fs.start_sector + @as(u64, block_num) * sectors_per_block;
    const buf: [*]const u8 = @ptrFromInt(virt);
    return blkdevice_if.write_sectors(fs.dev, lba, buf, sectors_per_block) == 0;
}

fn zeroBlock(fs: *Fs, block_num: u32) bool {
    var virt: u64 = undefined;
    var phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, fs.block_size, &virt, &phys) != 0) return false;
    defer phys_mem.free(pmm_if, phys);
    const bytes: [*]u8 = @ptrFromInt(virt);
    @memset(bytes[0..fs.block_size], 0);
    return writeBlock(fs, block_num, virt);
}

fn readBgDescUncached(fs: *Fs, group: u32) ?Ext2BgDesc {
    const descs_per_block = fs.block_size / @sizeOf(Ext2BgDesc);
    const block_num = fs.bgdt_block + group / descs_per_block;
    const buf = readBlock(fs, block_num) orelse return null;
    defer freeBlockBuf(buf);
    const descs: [*]const Ext2BgDesc = @ptrFromInt(buf.virt);
    return descs[group % descs_per_block];
}

/// Reads are served from `fs.bgdt_cache` when `group` is within it --
/// see `MAX_CACHED_GROUPS`.
fn readBgDesc(fs: *Fs, group: u32) ?Ext2BgDesc {
    if (group >= fs.groups_count) return null;
    if (group < MAX_CACHED_GROUPS) return fs.bgdt_cache[group];
    return readBgDescUncached(fs, group);
}

/// Always writes through to disk (a group-descriptor write only happens
/// on block/inode allocate-or-free, far rarer than reads), updating the
/// cache first when `group` is within it.
fn writeBgDesc(fs: *Fs, group: u32, desc: Ext2BgDesc) bool {
    if (group < MAX_CACHED_GROUPS) fs.bgdt_cache[group] = desc;

    const descs_per_block = fs.block_size / @sizeOf(Ext2BgDesc);
    const block_num = fs.bgdt_block + group / descs_per_block;
    const buf = readBlock(fs, block_num) orelse return false;
    defer freeBlockBuf(buf);
    const descs: [*]Ext2BgDesc = @ptrFromInt(buf.virt);
    descs[group % descs_per_block] = desc;
    return writeBlock(fs, block_num, buf.virt);
}

const SbCounter = enum { free_blocks, free_inodes };

/// Mutates the cached superblock in place and writes the *whole* cached
/// region straight back -- no disk read needed first, since the cache is
/// always kept current (loaded once at mount, every mutation goes
/// through here).
fn updateSuperblockCounter(fs: *Fs, which: SbCounter, delta: i32) void {
    const sb = sbView(fs);
    const field = switch (which) {
        .free_blocks => &sb.free_blocks_count,
        .free_inodes => &sb.free_inodes_count,
    };
    field.* = if (delta < 0) field.* - @as(u32, @intCast(-delta)) else field.* + @as(u32, @intCast(delta));

    // fs.sb_cache lives in this module's ordinary static memory, not an
    // HHDM-mapped phys_mem allocation -- passing it straight to
    // write_sectors would DMA from a bogus translated physical address
    // (see abi_types.BlkDevice's doc comment) instead of erroring, so the
    // write "succeeds" while silently writing zeros/garbage to the real
    // superblock LBA. Stage it through a scratch DMA buffer instead.
    var virt: u64 = undefined;
    var phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, 1024, &virt, &phys) != 0) return;
    defer phys_mem.free(pmm_if, phys);
    const scratch: [*]u8 = @ptrFromInt(virt);
    @memcpy(scratch[0..1024], &fs.sb_cache);

    _ = blkdevice_if.write_sectors(fs.dev, fs.start_sector + 2, scratch, 2);
}

fn readInode(fs: *Fs, inode_num: u32, out: *Ext2Inode) bool {
    if (inode_num < 1 or inode_num > fs.total_inodes) return false;
    const group = (inode_num - 1) / fs.inodes_per_group;
    const index = (inode_num - 1) % fs.inodes_per_group;
    const bg = readBgDesc(fs, group) orelse return false;

    const inode_offset = index * fs.inode_size;
    const inode_block = bg.inode_table + inode_offset / fs.block_size;
    const inode_block_offset = inode_offset % fs.block_size;

    const buf = readBlock(fs, inode_block) orelse return false;
    defer freeBlockBuf(buf);
    const src: [*]const u8 = @ptrFromInt(buf.virt + inode_block_offset);
    const dst: [*]u8 = @ptrCast(out);
    @memcpy(dst[0..@sizeOf(Ext2Inode)], src[0..@sizeOf(Ext2Inode)]);
    return true;
}

fn writeInode(fs: *Fs, inode_num: u32, inode: *const Ext2Inode) bool {
    if (inode_num < 1 or inode_num > fs.total_inodes) return false;
    const group = (inode_num - 1) / fs.inodes_per_group;
    const index = (inode_num - 1) % fs.inodes_per_group;
    const bg = readBgDesc(fs, group) orelse return false;

    const inode_offset = index * fs.inode_size;
    const inode_block = bg.inode_table + inode_offset / fs.block_size;
    const inode_block_offset = inode_offset % fs.block_size;

    const buf = readBlock(fs, inode_block) orelse return false;
    defer freeBlockBuf(buf);
    const dst: [*]u8 = @ptrFromInt(buf.virt + inode_block_offset);
    const src: [*]const u8 = @ptrCast(inode);
    @memcpy(dst[0..@sizeOf(Ext2Inode)], src[0..@sizeOf(Ext2Inode)]);
    return writeBlock(fs, inode_block, buf.virt);
}

// --- Block/inode allocation --------------------------------------------------

fn allocateBlock(fs: *Fs) u32 {
    var group: u32 = 0;
    while (group < fs.groups_count) : (group += 1) {
        var bg = readBgDesc(fs, group) orelse continue;
        if (bg.free_blocks_count == 0) continue;

        const bitmap_buf = readBlock(fs, bg.block_bitmap) orelse continue;
        const bitmap: [*]u8 = @ptrFromInt(bitmap_buf.virt);

        const blocks_in_group: u32 = if (group == fs.groups_count - 1)
            fs.total_blocks - group * fs.blocks_per_group
        else
            fs.blocks_per_group;

        var i: u32 = 0;
        while (i < blocks_in_group) : (i += 1) {
            if ((bitmap[i / 8] & (@as(u8, 1) << @intCast(i % 8))) == 0) {
                bitmap[i / 8] |= (@as(u8, 1) << @intCast(i % 8));
                _ = writeBlock(fs, bg.block_bitmap, bitmap_buf.virt);
                freeBlockBuf(bitmap_buf);

                bg.free_blocks_count -= 1;
                _ = writeBgDesc(fs, group, bg);
                updateSuperblockCounter(fs, .free_blocks, -1);

                return group * fs.blocks_per_group + i + fs.first_data_block;
            }
        }
        freeBlockBuf(bitmap_buf);
    }
    return 0;
}

fn freeBlock(fs: *Fs, block_num: u32) bool {
    if (block_num < fs.first_data_block or block_num >= fs.total_blocks) return false;
    const group = (block_num - fs.first_data_block) / fs.blocks_per_group;
    const index = (block_num - fs.first_data_block) % fs.blocks_per_group;

    var bg = readBgDesc(fs, group) orelse return false;
    const bitmap_buf = readBlock(fs, bg.block_bitmap) orelse return false;
    const bitmap: [*]u8 = @ptrFromInt(bitmap_buf.virt);

    if ((bitmap[index / 8] & (@as(u8, 1) << @intCast(index % 8))) == 0) {
        freeBlockBuf(bitmap_buf);
        return true;
    }

    bitmap[index / 8] &= ~(@as(u8, 1) << @intCast(index % 8));
    _ = writeBlock(fs, bg.block_bitmap, bitmap_buf.virt);
    freeBlockBuf(bitmap_buf);

    bg.free_blocks_count += 1;
    _ = writeBgDesc(fs, group, bg);
    updateSuperblockCounter(fs, .free_blocks, 1);
    return true;
}

fn allocateInode(fs: *Fs, is_directory: bool) u32 {
    var group: u32 = 0;
    while (group < fs.groups_count) : (group += 1) {
        var bg = readBgDesc(fs, group) orelse continue;
        if (bg.free_inodes_count == 0) continue;

        const bitmap_buf = readBlock(fs, bg.inode_bitmap) orelse continue;
        const bitmap: [*]u8 = @ptrFromInt(bitmap_buf.virt);

        var i: u32 = 0;
        while (i < fs.inodes_per_group) : (i += 1) {
            if (i == 0) continue; // inode 0 doesn't exist

            if ((bitmap[i / 8] & (@as(u8, 1) << @intCast(i % 8))) == 0) {
                bitmap[i / 8] |= (@as(u8, 1) << @intCast(i % 8));
                _ = writeBlock(fs, bg.inode_bitmap, bitmap_buf.virt);
                freeBlockBuf(bitmap_buf);

                bg.free_inodes_count -= 1;
                if (is_directory) bg.used_dirs_count += 1;
                _ = writeBgDesc(fs, group, bg);
                updateSuperblockCounter(fs, .free_inodes, -1);

                return group * fs.inodes_per_group + i + 1;
            }
        }
        freeBlockBuf(bitmap_buf);
    }
    return 0;
}

fn freeInode(fs: *Fs, inode_num: u32) bool {
    if (inode_num < 1 or inode_num > fs.total_inodes) return false;
    const group = (inode_num - 1) / fs.inodes_per_group;
    const index = (inode_num - 1) % fs.inodes_per_group;

    var bg = readBgDesc(fs, group) orelse return false;
    const bitmap_buf = readBlock(fs, bg.inode_bitmap) orelse return false;
    const bitmap: [*]u8 = @ptrFromInt(bitmap_buf.virt);

    if ((bitmap[index / 8] & (@as(u8, 1) << @intCast(index % 8))) == 0) {
        freeBlockBuf(bitmap_buf);
        return true;
    }

    bitmap[index / 8] &= ~(@as(u8, 1) << @intCast(index % 8));
    _ = writeBlock(fs, bg.inode_bitmap, bitmap_buf.virt);
    freeBlockBuf(bitmap_buf);

    bg.free_inodes_count += 1;
    _ = writeBgDesc(fs, group, bg);
    updateSuperblockCounter(fs, .free_inodes, 1);
    return true;
}

// --- Block pointer indirection -----------------------------------------------
//
// Direct blocks are `inode.block[0..12]`; `block[12]` is a single
// indirect pointer, `block[13]` a double indirect, `block[14]` a triple
// indirect. `readDoubleIndirect`/`readTripleIndirect` (and their `write*`
// counterparts) work over a contiguous *range* of logical pointers within
// one indirection subtree, zero-filling (read) or lazily allocating
// (write) any absent pointer block they cross -- an absent block at any
// level means every logical block pointer under it is a hole, not an
// error, matching real ext2 sparse-file semantics (see the file doc
// comment on the C reference's version of this bug).

fn readDoubleIndirect(fs: *Fs, dind_block: u32, start: u64, out: []u32) bool {
    const ppb = fs.block_size / 4;
    if (dind_block == 0) {
        @memset(out, 0);
        return true;
    }
    const dind_buf = readBlock(fs, dind_block) orelse return false;
    defer freeBlockBuf(dind_buf);
    const dind_ptrs: [*]const u32 = @ptrFromInt(dind_buf.virt);

    var done: usize = 0;
    var pos = start;
    while (done < out.len) {
        const first_level = pos / ppb;
        const second_level = pos % ppb;
        const to_read = @min(out.len - done, ppb - second_level);

        const ind_block = dind_ptrs[first_level];
        if (ind_block == 0) {
            @memset(out[done..][0..to_read], 0);
        } else {
            const ind_buf = readBlock(fs, ind_block) orelse return false;
            defer freeBlockBuf(ind_buf);
            const ind_ptrs: [*]const u32 = @ptrFromInt(ind_buf.virt);
            for (0..to_read) |i| out[done + i] = ind_ptrs[second_level + i];
        }

        done += to_read;
        pos += to_read;
    }
    return true;
}

fn readTripleIndirect(fs: *Fs, tind_block: u32, start: u64, out: []u32) bool {
    const ppb = fs.block_size / 4;
    const ppb2 = @as(u64, ppb) * ppb;
    if (tind_block == 0) {
        @memset(out, 0);
        return true;
    }
    const tind_buf = readBlock(fs, tind_block) orelse return false;
    defer freeBlockBuf(tind_buf);
    const tind_ptrs: [*]const u32 = @ptrFromInt(tind_buf.virt);

    var done: usize = 0;
    var pos = start;
    while (done < out.len) {
        const first_level = pos / ppb2;
        const rem = pos % ppb2;
        const to_read = @min(out.len - done, ppb2 - rem);

        if (!readDoubleIndirect(fs, tind_ptrs[first_level], rem, out[done..][0..to_read])) return false;

        done += to_read;
        pos += to_read;
    }
    return true;
}

/// Like `readDoubleIndirect`, but allocates+zeroes any absent index block
/// it needs to write through, and writes the (possibly newly-allocated)
/// root pointer back through `dind_block_ptr`.
fn writeDoubleIndirect(fs: *Fs, dind_block_ptr: *u32, start: u64, in_ptrs: []const u32) usize {
    const ppb = fs.block_size / 4;

    if (dind_block_ptr.* == 0) {
        const nb = allocateBlock(fs);
        if (nb == 0) return 0;
        if (!zeroBlock(fs, nb)) {
            _ = freeBlock(fs, nb);
            return 0;
        }
        dind_block_ptr.* = nb;
    }

    const dind_buf = readBlock(fs, dind_block_ptr.*) orelse return 0;
    const dind_ptrs: [*]u32 = @ptrFromInt(dind_buf.virt);

    var written: usize = 0;
    var pos = start;
    var dirty = false;

    while (written < in_ptrs.len) {
        const first_level = pos / ppb;
        const second_level = pos % ppb;
        const to_write = @min(in_ptrs.len - written, ppb - second_level);

        if (dind_ptrs[first_level] == 0) {
            const nb2 = allocateBlock(fs);
            if (nb2 == 0) break;
            if (!zeroBlock(fs, nb2)) {
                _ = freeBlock(fs, nb2);
                break;
            }
            dind_ptrs[first_level] = nb2;
            dirty = true;
        }

        const ind_buf = readBlock(fs, dind_ptrs[first_level]) orelse break;
        const ind_ptrs: [*]u32 = @ptrFromInt(ind_buf.virt);
        for (0..to_write) |i| ind_ptrs[second_level + i] = in_ptrs[written + i];
        const ok = writeBlock(fs, dind_ptrs[first_level], ind_buf.virt);
        freeBlockBuf(ind_buf);
        if (!ok) break;

        written += to_write;
        pos += to_write;
    }

    if (dirty) _ = writeBlock(fs, dind_block_ptr.*, dind_buf.virt);
    freeBlockBuf(dind_buf);
    return written;
}

fn writeTripleIndirect(fs: *Fs, tind_block_ptr: *u32, start: u64, in_ptrs: []const u32) usize {
    const ppb = fs.block_size / 4;
    const ppb2 = @as(u64, ppb) * ppb;

    if (tind_block_ptr.* == 0) {
        const nb = allocateBlock(fs);
        if (nb == 0) return 0;
        if (!zeroBlock(fs, nb)) {
            _ = freeBlock(fs, nb);
            return 0;
        }
        tind_block_ptr.* = nb;
    }

    const tind_buf = readBlock(fs, tind_block_ptr.*) orelse return 0;
    const tind_ptrs: [*]u32 = @ptrFromInt(tind_buf.virt);

    var written: usize = 0;
    var pos = start;
    var dirty = false;

    while (written < in_ptrs.len) {
        const first_level = pos / ppb2;
        const rem = pos % ppb2;
        const to_write = @min(in_ptrs.len - written, ppb2 - rem);

        const sub_written = writeDoubleIndirect(fs, &tind_ptrs[first_level], rem, in_ptrs[written..][0..to_write]);
        if (sub_written > 0) dirty = true;
        written += sub_written;
        pos += sub_written;
        if (sub_written < to_write) break;
    }

    if (dirty) _ = writeBlock(fs, tind_block_ptr.*, tind_buf.virt);
    freeBlockBuf(tind_buf);
    return written;
}

fn readBlockPointers(fs: *Fs, inode: *const Ext2Inode, block_idx_in: u32, out: []u32) usize {
    var block_idx = block_idx_in;
    var count = out.len;
    var read: usize = 0;
    const ppb = fs.block_size / 4;
    const ppb2 = @as(u64, ppb) * ppb;
    const ppb3 = ppb2 * ppb;

    if (block_idx < 12) {
        const to_read = @min(count, 12 - block_idx);
        for (0..to_read) |i| out[read + i] = inode.block[block_idx + i];
        read += to_read;
        block_idx += @intCast(to_read);
        count -= to_read;
    }

    if (count > 0 and block_idx < 12 + ppb) {
        const start = block_idx - 12;
        const to_read = @min(count, ppb - start);
        if (inode.block[12] == 0) {
            @memset(out[read..][0..to_read], 0);
        } else if (readBlock(fs, inode.block[12])) |buf| {
            defer freeBlockBuf(buf);
            const ptrs: [*]const u32 = @ptrFromInt(buf.virt);
            for (0..to_read) |i| out[read + i] = ptrs[start + i];
        } else {
            return read;
        }
        read += to_read;
        block_idx += @intCast(to_read);
        count -= to_read;
    }

    if (count > 0 and @as(u64, block_idx) < 12 + ppb + ppb2) {
        const start = @as(u64, block_idx) - 12 - ppb;
        const to_read = @min(count, ppb2 - start);
        if (!readDoubleIndirect(fs, inode.block[13], start, out[read..][0..to_read])) return read;
        read += to_read;
        block_idx += @intCast(to_read);
        count -= to_read;
    }

    if (count > 0 and @as(u64, block_idx) < 12 + ppb + ppb2 + ppb3) {
        const start = @as(u64, block_idx) - 12 - ppb - ppb2;
        const to_read = @min(count, ppb3 - start);
        if (!readTripleIndirect(fs, inode.block[14], start, out[read..][0..to_read])) return read;
        read += to_read;
    }

    return read;
}

fn writeBlockPointers(fs: *Fs, inode: *Ext2Inode, block_idx_in: u32, in_ptrs: []const u32) usize {
    var block_idx = block_idx_in;
    var count = in_ptrs.len;
    var written: usize = 0;
    const ppb = fs.block_size / 4;
    const ppb2 = @as(u64, ppb) * ppb;
    const ppb3 = ppb2 * ppb;

    if (block_idx < 12) {
        const to_write = @min(count, 12 - block_idx);
        for (0..to_write) |i| inode.block[block_idx + i] = in_ptrs[written + i];
        written += to_write;
        block_idx += @intCast(to_write);
        count -= to_write;
    }

    if (count > 0 and block_idx < 12 + ppb) {
        if (inode.block[12] == 0) {
            const nb = allocateBlock(fs);
            if (nb == 0) return written;
            if (!zeroBlock(fs, nb)) {
                _ = freeBlock(fs, nb);
                return written;
            }
            inode.block[12] = nb;
        }

        const start = block_idx - 12;
        if (readBlock(fs, inode.block[12])) |buf| {
            defer freeBlockBuf(buf);
            const ptrs: [*]u32 = @ptrFromInt(buf.virt);
            const to_write = @min(count, ppb - start);
            for (0..to_write) |i| ptrs[start + i] = in_ptrs[written + i];
            _ = writeBlock(fs, inode.block[12], buf.virt);
            written += to_write;
            block_idx += @intCast(to_write);
            count -= to_write;
        } else {
            return written;
        }
    }

    if (count > 0 and @as(u64, block_idx) < 12 + ppb + ppb2) {
        const start = @as(u64, block_idx) - 12 - ppb;
        const to_write = @min(count, ppb2 - start);
        const w = writeDoubleIndirect(fs, &inode.block[13], start, in_ptrs[written..][0..to_write]);
        written += w;
        block_idx += @intCast(w);
        count -= w;
        if (w < to_write) return written;
    }

    if (count > 0 and @as(u64, block_idx) < 12 + ppb + ppb2 + ppb3) {
        const start = @as(u64, block_idx) - 12 - ppb - ppb2;
        const to_write = @min(count, ppb3 - start);
        written += writeTripleIndirect(fs, &inode.block[14], start, in_ptrs[written..][0..to_write]);
    }

    return written;
}

fn countBlocksNeeded(fs: *Fs, size: u32) u32 {
    return (size + fs.block_size - 1) / fs.block_size;
}

/// Allocates and links one block at a time (rather than the C reference's
/// bulk `kmalloc`'d array of new block numbers), matching this module's
/// no-heap design -- see the file doc comment.
fn ensureBlocksAllocated(fs: *Fs, inode: *Ext2Inode, required_blocks: u32) bool {
    const current_blocks = countBlocksNeeded(fs, inode.size);
    if (required_blocks <= current_blocks) return true;

    var i = current_blocks;
    while (i < required_blocks) : (i += 1) {
        const block_num = allocateBlock(fs);
        if (block_num == 0) {
            var j = current_blocks;
            while (j < i) : (j += 1) {
                var freed: [1]u32 = undefined;
                if (readBlockPointers(fs, inode, j, &freed) == 1 and freed[0] != 0) {
                    _ = freeBlock(fs, freed[0]);
                }
            }
            return false;
        }

        // A freshly allocated block's on-disk content is whatever its
        // previous owner (if any) left there, not zero -- ext2 doesn't
        // guarantee free blocks are zeroed. Growing a file (via
        // `file_write` past EOF or `file_truncate`) must present zeros
        // in the newly-extended region regardless, matching POSIX
        // `ftruncate`/sparse-write semantics, so every block this
        // function hands out gets explicitly zeroed before being linked.
        if (!zeroBlock(fs, block_num)) {
            _ = freeBlock(fs, block_num);
            var j = current_blocks;
            while (j < i) : (j += 1) {
                var freed: [1]u32 = undefined;
                if (readBlockPointers(fs, inode, j, &freed) == 1 and freed[0] != 0) {
                    _ = freeBlock(fs, freed[0]);
                }
            }
            return false;
        }

        var ptr_arr = [_]u32{block_num};
        if (writeBlockPointers(fs, inode, i, &ptr_arr) != 1) {
            _ = freeBlock(fs, block_num);
            return false;
        }
    }
    return true;
}

// --- Directory entries --------------------------------------------------------

fn direntLen(name_len: usize) u16 {
    return @intCast(8 + ((name_len + 3) & ~@as(usize, 3)));
}

fn writeDirent(block_virt: u64, pos: u32, inode_num: u32, rec_len: u16, name: []const u8, file_type: u8) void {
    const entry: *Ext2DirentOnDisk = @ptrFromInt(block_virt + pos);
    entry.inode = inode_num;
    entry.rec_len = rec_len;
    entry.name_len = @intCast(name.len);
    entry.file_type = file_type;
    const name_dst: [*]u8 = @ptrFromInt(block_virt + pos + @sizeOf(Ext2DirentOnDisk));
    @memcpy(name_dst[0..name.len], name);
}

const FoundEntry = struct { inode: u32, file_type: u8 };

fn findEntry(fs: *Fs, dir_inode: u32, name: []const u8) ?FoundEntry {
    var inode: Ext2Inode = undefined;
    if (!readInode(fs, dir_inode, &inode)) return null;
    if (inode.mode & EXT2_S_IFDIR == 0) return null;

    var offset: u32 = 0;
    while (offset < inode.size) : (offset += fs.block_size) {
        const block_idx = offset / fs.block_size;
        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, &inode, block_idx, &block_num) != 1) return null;
        if (block_num[0] == 0) continue;

        const buf = readBlock(fs, block_num[0]) orelse return null;
        defer freeBlockBuf(buf);

        var pos: u32 = 0;
        while (pos < fs.block_size) {
            const entry: *const Ext2DirentOnDisk = @ptrFromInt(buf.virt + pos);
            if (entry.rec_len == 0) break;

            if (entry.inode != 0 and entry.name_len == name.len) {
                const name_ptr: [*]const u8 = @ptrFromInt(buf.virt + pos + @sizeOf(Ext2DirentOnDisk));
                if (std.mem.eql(u8, name_ptr[0..entry.name_len], name)) {
                    return .{ .inode = entry.inode, .file_type = entry.file_type };
                }
            }

            pos += entry.rec_len;
        }
    }
    return null;
}

fn addEntry(fs: *Fs, dir_inode: u32, name: []const u8, inode_num: u32, file_type: u8) bool {
    var inode: Ext2Inode = undefined;
    if (!readInode(fs, dir_inode, &inode)) return false;
    if (inode.mode & EXT2_S_IFDIR == 0) return false;

    const entry_len = direntLen(name.len);
    var offset: u32 = 0;

    while (offset < inode.size) : (offset += fs.block_size) {
        const block_idx = offset / fs.block_size;
        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, &inode, block_idx, &block_num) != 1) return false;

        if (block_num[0] == 0) {
            const new_block = allocateBlock(fs);
            if (new_block == 0) return false;
            if (!zeroBlock(fs, new_block)) {
                _ = freeBlock(fs, new_block);
                return false;
            }
            var ptr_arr = [_]u32{new_block};
            if (writeBlockPointers(fs, &inode, block_idx, &ptr_arr) != 1) {
                _ = freeBlock(fs, new_block);
                return false;
            }
            inode.size += fs.block_size;
            _ = writeInode(fs, dir_inode, &inode);
            block_num[0] = new_block;
        }

        const buf = readBlock(fs, block_num[0]) orelse return false;
        defer freeBlockBuf(buf);

        var pos: u32 = 0;
        while (pos < fs.block_size) {
            const entry: *Ext2DirentOnDisk = @ptrFromInt(buf.virt + pos);

            if (entry.rec_len == 0) {
                if (fs.block_size - pos >= entry_len) {
                    writeDirent(buf.virt, pos, inode_num, @intCast(fs.block_size - pos), name, file_type);
                    return writeBlock(fs, block_num[0], buf.virt);
                }
                break;
            }

            if (entry.inode == 0) {
                if (entry.rec_len >= entry_len) {
                    writeDirent(buf.virt, pos, inode_num, entry.rec_len, name, file_type);
                    return writeBlock(fs, block_num[0], buf.virt);
                }
            } else {
                const used_len = direntLen(entry.name_len);
                if (entry.rec_len >= used_len + entry_len) {
                    const old_rec_len = entry.rec_len;
                    entry.rec_len = used_len;
                    writeDirent(buf.virt, pos + used_len, inode_num, old_rec_len - used_len, name, file_type);
                    return writeBlock(fs, block_num[0], buf.virt);
                }
            }

            pos += entry.rec_len;
        }
    }

    // No room anywhere in the existing blocks: append a new one.
    const new_block = allocateBlock(fs);
    if (new_block == 0) return false;
    if (!zeroBlock(fs, new_block)) {
        _ = freeBlock(fs, new_block);
        return false;
    }

    const buf = readBlock(fs, new_block) orelse {
        _ = freeBlock(fs, new_block);
        return false;
    };
    writeDirent(buf.virt, 0, inode_num, @intCast(fs.block_size), name, file_type);
    const ok = writeBlock(fs, new_block, buf.virt);
    freeBlockBuf(buf);
    if (!ok) return false;

    const block_idx = inode.size / fs.block_size;
    var ptr_arr = [_]u32{new_block};
    if (writeBlockPointers(fs, &inode, block_idx, &ptr_arr) != 1) {
        _ = freeBlock(fs, new_block);
        return false;
    }

    inode.size += fs.block_size;
    return writeInode(fs, dir_inode, &inode);
}

/// `prev_pos` is scoped per-block (unlike the C reference, which kept a
/// single reused scratch buffer across the whole directory and so could
/// legally carry `prev_entry` across block boundaries) -- this module
/// allocates and frees a fresh block buffer every iteration, so holding a
/// pointer from a freed previous block would be a use-after-free.
/// Dirents never span block boundaries in ext2 anyway, so this changes no
/// observable behavior.
fn removeEntry(fs: *Fs, dir_inode: u32, name: []const u8) bool {
    var inode: Ext2Inode = undefined;
    if (!readInode(fs, dir_inode, &inode)) return false;
    if (inode.mode & EXT2_S_IFDIR == 0) return false;

    var offset: u32 = 0;
    while (offset < inode.size) : (offset += fs.block_size) {
        const block_idx = offset / fs.block_size;
        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, &inode, block_idx, &block_num) != 1) return false;
        if (block_num[0] == 0) continue;

        const buf = readBlock(fs, block_num[0]) orelse return false;
        defer freeBlockBuf(buf);

        var pos: u32 = 0;
        var prev_pos: ?u32 = null;
        while (pos < fs.block_size) {
            const entry: *Ext2DirentOnDisk = @ptrFromInt(buf.virt + pos);
            if (entry.rec_len == 0) break;

            if (entry.inode != 0 and entry.name_len == name.len) {
                const name_ptr: [*]const u8 = @ptrFromInt(buf.virt + pos + @sizeOf(Ext2DirentOnDisk));
                if (std.mem.eql(u8, name_ptr[0..entry.name_len], name)) {
                    entry.inode = 0;
                    if (prev_pos) |pp| {
                        const prev_entry: *Ext2DirentOnDisk = @ptrFromInt(buf.virt + pp);
                        prev_entry.rec_len += entry.rec_len;
                    }
                    return writeBlock(fs, block_num[0], buf.virt);
                }
            }

            prev_pos = pos;
            pos += entry.rec_len;
        }
    }
    return false;
}

/// Lists the `index`-th entry of `dir_inode` in on-disk order, skipping
/// `.`/`..` (see the file doc comment on why those are excluded).
fn listEntryAt(fs: *Fs, dir_inode: u32, index: u32, entry_out: *abi.Ext2DirEntry) c_int {
    var inode: Ext2Inode = undefined;
    if (!readInode(fs, dir_inode, &inode)) return abi.ENOENT;
    if (inode.mode & EXT2_S_IFDIR == 0) return abi.ENOTDIR;

    var seen: u32 = 0;
    var offset: u32 = 0;
    while (offset < inode.size) : (offset += fs.block_size) {
        const block_idx = offset / fs.block_size;
        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, &inode, block_idx, &block_num) != 1) return abi.ENOENT;
        if (block_num[0] == 0) continue;

        const buf = readBlock(fs, block_num[0]) orelse return abi.EIO;
        defer freeBlockBuf(buf);

        var pos: u32 = 0;
        while (pos < fs.block_size) {
            const entry: *const Ext2DirentOnDisk = @ptrFromInt(buf.virt + pos);
            if (entry.rec_len == 0) break;

            if (entry.inode != 0) {
                const name_ptr: [*]const u8 = @ptrFromInt(buf.virt + pos + @sizeOf(Ext2DirentOnDisk));
                const name = name_ptr[0..entry.name_len];
                if (!std.mem.eql(u8, name, ".") and !std.mem.eql(u8, name, "..")) {
                    if (seen == index) {
                        entry_out.* = .{};
                        entry_out.inode = entry.inode;
                        entry_out.file_type = entry.file_type;
                        entry_out.name_len = entry.name_len;
                        @memcpy(entry_out.name[0..name.len], name);
                        entry_out.name[name.len] = 0;
                        return 0;
                    }
                    seen += 1;
                }
            }

            pos += entry.rec_len;
        }
    }
    return abi.ENOENT;
}

fn writeDotEntries(fs: *Fs, block_num: u32, self_inode: u32, parent_inode: u32) bool {
    if (!zeroBlock(fs, block_num)) return false;
    const buf = readBlock(fs, block_num) orelse return false;
    defer freeBlockBuf(buf);

    const dot_len = direntLen(1);
    writeDirent(buf.virt, 0, self_inode, dot_len, ".", abi.EXT2_FT_DIR);
    writeDirent(buf.virt, dot_len, parent_inode, @intCast(fs.block_size - dot_len), "..", abi.EXT2_FT_DIR);
    return writeBlock(fs, block_num, buf.virt);
}

/// Rejects empty names, names over `EXT2_NAME_LEN`, and the reserved
/// `.`/`..` -- real ext2 (and every POSIX filesystem) refuses to let a
/// caller create/rename onto either of those, since they're not regular
/// entries a directory owns, they're the fixed structural links every
/// directory already has.
fn validateName(name: []const u8) c_int {
    if (name.len == 0) return abi.EINVAL;
    if (name.len > abi.EXT2_NAME_LEN) return abi.ENAMETOOLONG;
    if (std.mem.eql(u8, name, ".") or std.mem.eql(u8, name, "..")) return abi.EINVAL;
    return 0;
}

/// Frees every data block an inode owns -- shared by file/symlink/dir
/// deletion. Fast symlinks (target stored directly in `inode.block[]`,
/// see `FAST_SYMLINK_MAX_LEN`) and device/FIFO/socket nodes never
/// allocate real data blocks, so there's nothing to free for those; a
/// slow symlink's `block[]` is a normal indirection tree and frees like
/// any small file's.
fn freeAllInodeBlocks(fs: *Fs, inode: *const Ext2Inode) void {
    const ft = inode.mode & EXT2_S_IFMT;
    if (ft == EXT2_S_IFLNK and inode.size <= FAST_SYMLINK_MAX_LEN) return;
    if (ft == EXT2_S_IFCHR or ft == EXT2_S_IFBLK or ft == EXT2_S_IFIFO or ft == EXT2_S_IFSOCK) return;

    const blocks_count = countBlocksNeeded(fs, inode.size);
    var i: u32 = 0;
    while (i < blocks_count) : (i += 1) {
        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, inode, i, &block_num) == 1 and block_num[0] != 0) {
            _ = freeBlock(fs, block_num[0]);
        }
    }
}

// --- Data read/write core, shared by file_read/write and slow symlinks -----

fn readDataBlocks(fs: *Fs, inode: *const Ext2Inode, offset: u64, buf: [*]u8, count_in: u64) u64 {
    if (offset >= inode.size) return 0;
    var count = count_in;
    if (offset + count > inode.size) count = inode.size - offset;

    var bytes_read: u64 = 0;
    var pos = offset;
    while (count > 0) {
        const block_idx: u32 = @intCast(pos / fs.block_size);
        const block_offset: u32 = @intCast(pos % fs.block_size);
        const to_read = @min(count, fs.block_size - block_offset);

        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, inode, block_idx, &block_num) != 1) break;

        if (block_num[0] == 0) {
            @memset(buf[bytes_read..][0..to_read], 0);
        } else {
            const buf_blk = readBlock(fs, block_num[0]) orelse break;
            defer freeBlockBuf(buf_blk);
            const src: [*]const u8 = @ptrFromInt(buf_blk.virt + block_offset);
            @memcpy(buf[bytes_read..][0..to_read], src[0..to_read]);
        }

        bytes_read += to_read;
        pos += to_read;
        count -= to_read;
    }
    return bytes_read;
}

fn writeDataBlocks(fs: *Fs, inode: *const Ext2Inode, offset: u64, src: [*]const u8, count_in: u64) u64 {
    var count = count_in;
    var bytes_written: u64 = 0;
    var pos = offset;

    while (count > 0) {
        const block_idx: u32 = @intCast(pos / fs.block_size);
        const block_offset: u32 = @intCast(pos % fs.block_size);
        const to_write = @min(count, fs.block_size - block_offset);

        var block_num: [1]u32 = undefined;
        if (readBlockPointers(fs, inode, block_idx, &block_num) != 1 or block_num[0] == 0) break;

        const buf_blk = readBlock(fs, block_num[0]) orelse break;
        const dst: [*]u8 = @ptrFromInt(buf_blk.virt + block_offset);
        @memcpy(dst[0..to_write], src[bytes_written..][0..to_write]);
        const ok = writeBlock(fs, block_num[0], buf_blk.virt);
        freeBlockBuf(buf_blk);
        if (!ok) break;

        bytes_written += to_write;
        pos += to_write;
        count -= to_write;
    }
    return bytes_written;
}

/// Allocates whatever blocks `target`'s length needs and writes it in,
/// without touching `.mode`/`.links_count` -- used by `symlinkCreate`'s
/// slow-symlink path on a not-yet-persisted inode.
fn writeWholeData(fs: *Fs, inode: *Ext2Inode, data: []const u8) bool {
    const required_blocks = countBlocksNeeded(fs, @intCast(data.len));
    if (!ensureBlocksAllocated(fs, inode, required_blocks)) return false;
    const written = writeDataBlocks(fs, inode, 0, data.ptr, data.len);
    if (written != data.len) return false;
    inode.size = @intCast(data.len);
    return true;
}

/// True if `candidate` is `start` itself or one of its ancestors (walking
/// `..` links up to the root) -- used by `renameImpl` to refuse moving a
/// directory into its own subtree, which would otherwise detach it from
/// the tree entirely (unreachable from root, but still "linked" and
/// leaking space forever). Bounded to guard against a corrupt `..` chain
/// looping forever rather than terminating at root.
fn isAncestorOrSelf(fs: *Fs, candidate: u32, start: u32) bool {
    var current = start;
    var hops: u32 = 0;
    while (hops < 4096) : (hops += 1) {
        if (current == candidate) return true;
        if (current == abi.EXT2_ROOT_INO) return false;
        const parent = findEntry(fs, current, "..") orelse return false;
        current = parent.inode;
    }
    return true; // pathological depth: refuse rather than risk a false negative
}

// --- Public API (abi.Ext2) ---------------------------------------------------

pub fn mount(dev: ?*anyopaque, partition_start_lba: u64, partition_end_lba: u64, out_fs: *?*anyopaque) callconv(.c) c_int {
    if (dev == null) return abi.EINVAL;
    if (s_fs_count >= MAX_FS) return abi.ENOMEM;

    var sb_virt: u64 = undefined;
    var sb_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, 1024, &sb_virt, &sb_phys) != 0) return abi.ENOMEM;
    defer phys_mem.free(pmm_if, sb_phys);

    const sb_buf: [*]u8 = @ptrFromInt(sb_virt);
    if (blkdevice_if.read_sectors(dev, partition_start_lba + 2, sb_buf, 2) != 0) return abi.EIO;

    const sb: *const Ext2Superblock = @ptrCast(@alignCast(sb_buf));
    if (sb.magic != EXT2_SIGNATURE) return abi.EIO;
    if (sb.blocks_per_group == 0 or sb.inodes_per_group == 0) return abi.EIO;

    const fs = &s_filesystems[s_fs_count];
    s_fs_count += 1;

    fs.* = .{};
    fs.dev = dev;
    fs.start_sector = partition_start_lba;
    fs.end_sector = partition_end_lba;
    fs.block_size = @as(u32, 1024) << @intCast(sb.log_block_size);
    fs.blocks_per_group = sb.blocks_per_group;
    fs.inodes_per_group = sb.inodes_per_group;
    fs.first_data_block = sb.first_data_block;
    fs.total_blocks = sb.blocks_count;
    fs.total_inodes = sb.inodes_count;
    fs.groups_count = (sb.blocks_count + sb.blocks_per_group - 1) / sb.blocks_per_group;
    fs.bgdt_block = if (sb.first_data_block == 0) 1 else sb.first_data_block + 1;
    fs.inode_size = if (sb.rev_level >= 1) sb.inode_size else 128;

    @memcpy(&fs.sb_cache, sb_buf[0..1024]);

    var g: u32 = 0;
    while (g < fs.groups_count and g < MAX_CACHED_GROUPS) : (g += 1) {
        fs.bgdt_cache[g] = readBgDescUncached(fs, g) orelse {
            s_fs_count -= 1;
            return abi.EIO;
        };
    }

    out_fs.* = fs;
    return 0;
}

pub fn unmount(fs_opaque: ?*anyopaque) callconv(.c) c_int {
    _ = asFs(fs_opaque) orelse return abi.EINVAL;
    return 0;
}

pub fn rootInode(fs_opaque: ?*anyopaque) callconv(.c) u32 {
    _ = fs_opaque;
    return abi.EXT2_ROOT_INO;
}

pub fn lookup(fs_opaque: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, inode_out: *u32, file_type_out: *u8) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const found = findEntry(fs, dir_inode, std.mem.sliceTo(name, 0)) orelse return abi.ENOENT;
    inode_out.* = found.inode;
    file_type_out.* = found.file_type;
    return 0;
}

fn statImpl(fs: *Fs, inode_num: u32, out: *abi.Ext2Stat) c_int {
    var inode: Ext2Inode = undefined;
    if (!readInode(fs, inode_num, &inode)) return abi.ENOENT;

    const ft = inode.mode & EXT2_S_IFMT;
    const rdev: u32 = if (ft == EXT2_S_IFCHR or ft == EXT2_S_IFBLK) inode.block[0] else 0;

    out.* = .{
        .inode_num = inode_num,
        .mode = inode.mode,
        .size = inode.size,
        .links_count = inode.links_count,
        .atime = inode.atime,
        .mtime = inode.mtime,
        .ctime = inode.ctime,
        .rdev = rdev,
    };
    return 0;
}

pub fn stat(fs_opaque: ?*anyopaque, inode_num: u32, out: *abi.Ext2Stat) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return statImpl(fs, inode_num, out);
}

pub fn fileCreate(fs_opaque: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, mode: u16, inode_out: *u32) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const name_slice = std.mem.sliceTo(name, 0);
    const vrc = validateName(name_slice);
    if (vrc != 0) return vrc;

    if (findEntry(fs, dir_inode, name_slice) != null) return abi.EEXIST;

    const new_inode_num = allocateInode(fs, false);
    if (new_inode_num == 0) return abi.ENOSPC;

    var inode: Ext2Inode = std.mem.zeroes(Ext2Inode);
    inode.mode = EXT2_S_IFREG | (mode & 0x0FFF);
    inode.atime = timeNow();
    inode.ctime = timeNow();
    inode.mtime = timeNow();
    inode.links_count = 1;

    if (!writeInode(fs, new_inode_num, &inode)) {
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }
    if (!addEntry(fs, dir_inode, name_slice, new_inode_num, abi.EXT2_FT_REG_FILE)) {
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    inode_out.* = new_inode_num;
    return 0;
}

/// Unlinks `(dir_inode, name)` regardless of its type, as long as it
/// isn't a directory (use `dir_delete` for those) -- covers regular
/// files, symlinks, and device/FIFO/socket nodes uniformly, since freeing
/// their blocks (`freeAllInodeBlocks`) already knows how to do the right
/// thing for each.
fn fileDeleteImpl(fs: *Fs, dir_inode: u32, name: []const u8) c_int {
    const found = findEntry(fs, dir_inode, name) orelse return abi.ENOENT;
    if (found.file_type == abi.EXT2_FT_DIR) return abi.EISDIR;

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, found.inode, &inode)) return abi.EIO;

    if (!removeEntry(fs, dir_inode, name)) return abi.EIO;

    freeAllInodeBlocks(fs, &inode);
    _ = freeInode(fs, found.inode);
    return 0;
}

pub fn fileDelete(fs_opaque: ?*anyopaque, dir_inode: u32, name: [*:0]const u8) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return fileDeleteImpl(fs, dir_inode, std.mem.sliceTo(name, 0));
}

fn fileReadImpl(fs: *Fs, inode_num: u32, offset: u64, buf: ?*anyopaque, count: u64, bytes_read_out: *u64) c_int {
    bytes_read_out.* = 0;
    if (buf == null) return abi.EINVAL;

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, inode_num, &inode)) return abi.ENOENT;
    if (inode.mode & EXT2_S_IFMT == EXT2_S_IFDIR) return abi.EISDIR;
    if (inode.mode & EXT2_S_IFMT != EXT2_S_IFREG) return abi.EINVAL;

    const dst: [*]u8 = @ptrCast(buf.?);
    const bytes_read = readDataBlocks(fs, &inode, offset, dst, count);

    inode.atime = timeNow();
    _ = writeInode(fs, inode_num, &inode);

    bytes_read_out.* = bytes_read;
    return 0;
}

pub fn fileRead(fs_opaque: ?*anyopaque, inode_num: u32, offset: u64, buf: ?*anyopaque, count: u64, bytes_read_out: *u64) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return fileReadImpl(fs, inode_num, offset, buf, count, bytes_read_out);
}

fn fileWriteImpl(fs: *Fs, inode_num: u32, offset: u64, buf: ?*const anyopaque, count_in: u64, bytes_written_out: *u64) c_int {
    bytes_written_out.* = 0;
    if (buf == null) return abi.EINVAL;

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, inode_num, &inode)) return abi.ENOENT;
    if (inode.mode & EXT2_S_IFMT == EXT2_S_IFDIR) return abi.EISDIR;
    if (inode.mode & EXT2_S_IFMT != EXT2_S_IFREG) return abi.EINVAL;

    const required_blocks = countBlocksNeeded(fs, @intCast(offset + count_in));
    if (!ensureBlocksAllocated(fs, &inode, required_blocks)) return abi.ENOSPC;

    const src: [*]const u8 = @ptrCast(buf.?);
    const bytes_written = writeDataBlocks(fs, &inode, offset, src, count_in);
    const pos = offset + bytes_written;

    if (pos > inode.size) inode.size = @intCast(pos);
    inode.mtime = timeNow();
    inode.ctime = timeNow();
    _ = writeInode(fs, inode_num, &inode);

    bytes_written_out.* = bytes_written;
    return 0;
}

pub fn fileWrite(fs_opaque: ?*anyopaque, inode_num: u32, offset: u64, buf: ?*const anyopaque, count: u64, bytes_written_out: *u64) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return fileWriteImpl(fs, inode_num, offset, buf, count, bytes_written_out);
}

pub fn fileTruncate(fs_opaque: ?*anyopaque, inode_num: u32, length: u64) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, inode_num, &inode)) return abi.ENOENT;
    if (inode.mode & EXT2_S_IFMT == EXT2_S_IFDIR) return abi.EISDIR;
    if (inode.mode & EXT2_S_IFMT != EXT2_S_IFREG) return abi.EINVAL;

    if (length != inode.size) {
        if (length > inode.size) {
            const required_blocks = countBlocksNeeded(fs, @intCast(length));
            const current_blocks = countBlocksNeeded(fs, inode.size);
            if (required_blocks > current_blocks and !ensureBlocksAllocated(fs, &inode, required_blocks)) {
                return abi.ENOSPC;
            }
        } else {
            const old_blocks = countBlocksNeeded(fs, inode.size);
            const new_blocks = countBlocksNeeded(fs, @intCast(length));
            if (new_blocks < old_blocks) {
                var i = new_blocks;
                while (i < old_blocks) : (i += 1) {
                    var block_num: [1]u32 = undefined;
                    if (readBlockPointers(fs, &inode, i, &block_num) == 1 and block_num[0] != 0) {
                        _ = freeBlock(fs, block_num[0]);
                    }
                }
                var zero_arr: [1]u32 = .{0};
                i = new_blocks;
                while (i < old_blocks) : (i += 1) {
                    _ = writeBlockPointers(fs, &inode, i, &zero_arr);
                }
            }
        }
        inode.size = @intCast(length);
    }

    inode.mtime = timeNow();
    inode.ctime = timeNow();
    if (!writeInode(fs, inode_num, &inode)) return abi.EIO;
    return 0;
}

pub fn dirCreate(fs_opaque: ?*anyopaque, parent_inode: u32, name: [*:0]const u8, mode: u16, inode_out: *u32) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const name_slice = std.mem.sliceTo(name, 0);
    const vrc = validateName(name_slice);
    if (vrc != 0) return vrc;

    if (findEntry(fs, parent_inode, name_slice) != null) return abi.EEXIST;

    const new_inode_num = allocateInode(fs, true);
    if (new_inode_num == 0) return abi.ENOSPC;

    const first_block = allocateBlock(fs);
    if (first_block == 0) {
        _ = freeInode(fs, new_inode_num);
        return abi.ENOSPC;
    }

    if (!writeDotEntries(fs, first_block, new_inode_num, parent_inode)) {
        _ = freeBlock(fs, first_block);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    var inode: Ext2Inode = std.mem.zeroes(Ext2Inode);
    inode.mode = EXT2_S_IFDIR | (mode & 0x0FFF);
    inode.size = fs.block_size;
    inode.atime = timeNow();
    inode.ctime = timeNow();
    inode.mtime = timeNow();
    inode.links_count = 2;
    inode.block[0] = first_block;

    if (!writeInode(fs, new_inode_num, &inode)) {
        _ = freeBlock(fs, first_block);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }
    if (!addEntry(fs, parent_inode, name_slice, new_inode_num, abi.EXT2_FT_DIR)) {
        _ = freeBlock(fs, first_block);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    var parent: Ext2Inode = undefined;
    if (readInode(fs, parent_inode, &parent)) {
        parent.links_count += 1;
        parent.mtime = timeNow();
        parent.ctime = timeNow();
        _ = writeInode(fs, parent_inode, &parent);
    }

    inode_out.* = new_inode_num;
    return 0;
}

pub fn dirDelete(fs_opaque: ?*anyopaque, parent_inode: u32, name: [*:0]const u8) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const name_slice = std.mem.sliceTo(name, 0);
    const found = findEntry(fs, parent_inode, name_slice) orelse return abi.ENOENT;
    if (found.file_type != abi.EXT2_FT_DIR) return abi.ENOTDIR;

    var tmp: abi.Ext2DirEntry = undefined;
    if (listEntryAt(fs, found.inode, 0, &tmp) == 0) return abi.ENOTEMPTY;

    if (!removeEntry(fs, parent_inode, name_slice)) return abi.EIO;

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, found.inode, &inode)) return abi.EIO;

    freeAllInodeBlocks(fs, &inode);
    _ = freeInode(fs, found.inode);

    var parent: Ext2Inode = undefined;
    if (readInode(fs, parent_inode, &parent)) {
        if (parent.links_count > 0) parent.links_count -= 1;
        parent.mtime = timeNow();
        parent.ctime = timeNow();
        _ = writeInode(fs, parent_inode, &parent);
    }

    return 0;
}

pub fn dirRead(fs_opaque: ?*anyopaque, dir_inode: u32, index: u32, entry_out: *abi.Ext2DirEntry) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return listEntryAt(fs, dir_inode, index, entry_out);
}

/// Handles moving a directory to a new parent by fixing up its `..` entry
/// and both parents' `links_count` (the C reference didn't support
/// renaming directories across parents at all -- `links_count` and the
/// moved directory's own `..` would otherwise silently go stale), and
/// refuses to move a directory into its own subtree (`isAncestorOrSelf`).
fn renameImpl(fs: *Fs, old_dir_inode: u32, old_name: []const u8, new_dir_inode: u32, new_name: []const u8) c_int {
    var rc = validateName(old_name);
    if (rc != 0) return rc;
    rc = validateName(new_name);
    if (rc != 0) return rc;

    if (old_dir_inode == new_dir_inode and std.mem.eql(u8, old_name, new_name)) return 0;

    const found = findEntry(fs, old_dir_inode, old_name) orelse return abi.ENOENT;
    if (findEntry(fs, new_dir_inode, new_name) != null) return abi.EEXIST;

    if (found.file_type == abi.EXT2_FT_DIR and isAncestorOrSelf(fs, found.inode, new_dir_inode)) {
        return abi.EINVAL;
    }

    if (!removeEntry(fs, old_dir_inode, old_name)) return abi.EIO;
    if (!addEntry(fs, new_dir_inode, new_name, found.inode, found.file_type)) {
        _ = addEntry(fs, old_dir_inode, old_name, found.inode, found.file_type);
        return abi.EIO;
    }

    if (found.file_type == abi.EXT2_FT_DIR and old_dir_inode != new_dir_inode) {
        _ = removeEntry(fs, found.inode, "..");
        _ = addEntry(fs, found.inode, "..", new_dir_inode, abi.EXT2_FT_DIR);

        var old_parent: Ext2Inode = undefined;
        if (readInode(fs, old_dir_inode, &old_parent)) {
            if (old_parent.links_count > 0) old_parent.links_count -= 1;
            _ = writeInode(fs, old_dir_inode, &old_parent);
        }
        var new_parent: Ext2Inode = undefined;
        if (readInode(fs, new_dir_inode, &new_parent)) {
            new_parent.links_count += 1;
            _ = writeInode(fs, new_dir_inode, &new_parent);
        }
    }

    return 0;
}

pub fn rename(fs_opaque: ?*anyopaque, old_dir_inode: u32, old_name: [*:0]const u8, new_dir_inode: u32, new_name: [*:0]const u8) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();
    return renameImpl(fs, old_dir_inode, std.mem.sliceTo(old_name, 0), new_dir_inode, std.mem.sliceTo(new_name, 0));
}

pub fn symlinkCreate(fs_opaque: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, target: [*:0]const u8, inode_out: *u32) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const name_slice = std.mem.sliceTo(name, 0);
    const target_slice = std.mem.sliceTo(target, 0);

    const vrc = validateName(name_slice);
    if (vrc != 0) return vrc;
    if (target_slice.len == 0) return abi.EINVAL;
    if (target_slice.len > abi.EXT2_NAME_LEN * 8) return abi.ENAMETOOLONG; // generous cap, not a hard ext2 limit

    if (findEntry(fs, dir_inode, name_slice) != null) return abi.EEXIST;

    const new_inode_num = allocateInode(fs, false);
    if (new_inode_num == 0) return abi.ENOSPC;

    var inode: Ext2Inode = std.mem.zeroes(Ext2Inode);
    inode.mode = EXT2_S_IFLNK | 0x1FF; // symlinks conventionally carry mode 0777; the kernel never checks it
    inode.atime = timeNow();
    inode.ctime = timeNow();
    inode.mtime = timeNow();
    inode.links_count = 1;

    if (target_slice.len <= FAST_SYMLINK_MAX_LEN) {
        const block_bytes: [*]u8 = @ptrCast(&inode.block);
        @memcpy(block_bytes[0..target_slice.len], target_slice);
        inode.size = @intCast(target_slice.len);
    } else if (!writeWholeData(fs, &inode, target_slice)) {
        freeAllInodeBlocks(fs, &inode);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    if (!writeInode(fs, new_inode_num, &inode)) {
        freeAllInodeBlocks(fs, &inode);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }
    if (!addEntry(fs, dir_inode, name_slice, new_inode_num, abi.EXT2_FT_SYMLINK)) {
        freeAllInodeBlocks(fs, &inode);
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    inode_out.* = new_inode_num;
    return 0;
}

pub fn symlinkRead(fs_opaque: ?*anyopaque, inode_num: u32, buf: [*]u8, buf_len: u64, len_out: *u64) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    var inode: Ext2Inode = undefined;
    if (!readInode(fs, inode_num, &inode)) return abi.ENOENT;
    if (inode.mode & EXT2_S_IFMT != EXT2_S_IFLNK) return abi.EINVAL;

    const target_len: u64 = inode.size;
    const to_copy = @min(target_len, buf_len);

    if (target_len <= FAST_SYMLINK_MAX_LEN) {
        const block_bytes: [*]const u8 = @ptrCast(&inode.block);
        @memcpy(buf[0..to_copy], block_bytes[0..to_copy]);
    } else {
        _ = readDataBlocks(fs, &inode, 0, buf, to_copy);
    }

    len_out.* = to_copy;
    return 0;
}

pub fn mknod(fs_opaque: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, mode: u16, file_type: u8, dev: u32, inode_out: *u32) callconv(.c) c_int {
    const fs = asFs(fs_opaque) orelse return abi.EINVAL;
    fs.lock.lock();
    defer fs.lock.unlock();

    const name_slice = std.mem.sliceTo(name, 0);
    const vrc = validateName(name_slice);
    if (vrc != 0) return vrc;

    const type_bits: u16 = switch (file_type) {
        abi.EXT2_FT_CHRDEV => EXT2_S_IFCHR,
        abi.EXT2_FT_BLKDEV => EXT2_S_IFBLK,
        abi.EXT2_FT_FIFO => EXT2_S_IFIFO,
        abi.EXT2_FT_SOCK => EXT2_S_IFSOCK,
        else => return abi.EINVAL,
    };

    if (findEntry(fs, dir_inode, name_slice) != null) return abi.EEXIST;

    const new_inode_num = allocateInode(fs, false);
    if (new_inode_num == 0) return abi.ENOSPC;

    var inode: Ext2Inode = std.mem.zeroes(Ext2Inode);
    inode.mode = type_bits | (mode & 0x0FFF);
    inode.atime = timeNow();
    inode.ctime = timeNow();
    inode.mtime = timeNow();
    inode.links_count = 1;
    if (file_type == abi.EXT2_FT_CHRDEV or file_type == abi.EXT2_FT_BLKDEV) {
        inode.block[0] = dev;
    }

    if (!writeInode(fs, new_inode_num, &inode)) {
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }
    if (!addEntry(fs, dir_inode, name_slice, new_inode_num, file_type)) {
        _ = freeInode(fs, new_inode_num);
        return abi.EIO;
    }

    inode_out.* = new_inode_num;
    return 0;
}

comptime {
    abi.exportInterface("ext2", abi.Ext2, .{
        .mount = mount,
        .unmount = unmount,
        .root_inode = rootInode,
        .lookup = lookup,
        .stat = stat,
        .file_create = fileCreate,
        .file_delete = fileDelete,
        .file_read = fileRead,
        .file_write = fileWrite,
        .file_truncate = fileTruncate,
        .dir_create = dirCreate,
        .dir_delete = dirDelete,
        .dir_read = dirRead,
        .rename = rename,
        .symlink_create = symlinkCreate,
        .symlink_read = symlinkRead,
        .mknod = mknod,
    });
}

comptime {
    _ = @import("test.zig");
}
