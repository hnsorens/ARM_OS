//! AArch64 4-level page table engine, exporting `Mmu` (category "mmu").
//! Table frames are allocated through `pmm` and accessed via their HHDM
//! alias, matching the bootloader's own page tables in descriptor layout.
//!
//! Two real bugs in the C reference (modules/hnsorens/memory/mmu/pt.c)
//! were fixed here rather than ported faithfully -- see the comments at
//! `buildLeafFlags` and `protectSinglePage`.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

const pmm_if = abi.importInterfaceAny(abi.Pmm);
const serial_if = abi.importInterfaceAny(abi.Serial);

const ARM_TABLE_DESCRIPTOR: u64 = 0x3;
const ARM_PAGE_DESCRIPTOR: u64 = 0x3;
const ARM_ACCESS_FLAG: u64 = 1 << 10;
const ARM_SH_INNER_SHAREABLE: u64 = 0x3 << 8;

const PAGE_MASK: u64 = ~@as(u64, 0xFFF);
const PTE_ADDR_MASK: u64 = 0x0000FFFFFFFFF000;
const PTE_RETURN_FLAG_MASK: u64 = abi.MMU_RO | abi.MMU_USER | abi.MMU_NO_EXEC | abi.MMU_NOCACHE | abi.MMU_WRITE_THROUGH;
const TLB_BATCH_THRESHOLD: u64 = 64;

const PS_4KB: u64 = 0x1000;
const PS_2MB: u64 = 0x200000;
const PS_1GB: u64 = 0x40000000;

fn pteValid(pte: u64) bool {
    return (pte & 1) != 0;
}

/// True if `pte` is a table (non-leaf) descriptor, as opposed to a block
/// leaf at L1/L2 -- both encode bit 0 (valid), differing only in bit 1.
fn isTableEntry(pte: u64) bool {
    return (pte & 0x3) == ARM_TABLE_DESCRIPTOR;
}

fn l2v(phys: u64) [*]u64 {
    return @ptrFromInt(phys + abi.HHDM_OFFSET);
}

const PtIndices = struct {
    l0: u9,
    l1: u9,
    l2: u9,
    l3: u9,
    offset: u16,
};

fn extractIndices(virt: u64) PtIndices {
    return .{
        .offset = @truncate(virt & 0xFFF),
        .l3 = @truncate(virt >> 12),
        .l2 = @truncate(virt >> 21),
        .l1 = @truncate(virt >> 30),
        .l0 = @truncate(virt >> 39),
    };
}

fn isPtEmpty(pt_phys: u64) bool {
    const table = l2v(pt_phys);
    for (0..512) |i| {
        if (table[i] != 0) return false;
    }
    return true;
}

/// `f` is the caller-supplied `mmu_flags` bitmask (RO/USER/NO_EXEC/
/// NOCACHE/WRITE_THROUGH), which occupies bits that never overlap the
/// descriptor-type bits (0-1) or AF/SH (bits 8-10). The C driver only
/// ever OR'd `f` in directly, never setting AF or SH itself -- meaning
/// every page it ever mapped would raise an Access Flag fault on first
/// real hardware access. Always set them here.
fn buildLeafFlags(f: u64) u64 {
    return f | ARM_ACCESS_FLAG | ARM_SH_INNER_SHAREABLE;
}

fn getOrAllocTable(entry: *u64) ?u64 {
    if (pteValid(entry.*)) return entry.* & PAGE_MASK;

    var tbl_phys: u64 = undefined;
    if (pmm_if.alloc_page(0, &tbl_phys) != 0) return null;
    const tbl = l2v(tbl_phys);
    @memset(tbl[0..512], 0);
    entry.* = tbl_phys | ARM_TABLE_DESCRIPTOR;
    return tbl_phys;
}

// --- Teardown (free) ------------------------------------------------------

fn freeL2Table(l2: [*]u64) void {
    for (0..512) |k| {
        if (pteValid(l2[k]) and isTableEntry(l2[k])) {
            _ = pmm_if.release(l2[k] & PAGE_MASK);
        }
    }
}

fn freeL1Table(l1: [*]u64) void {
    for (0..512) |j| {
        if (!pteValid(l1[j]) or !isTableEntry(l1[j])) continue;
        freeL2Table(l2v(l1[j] & PAGE_MASK));
        _ = pmm_if.release(l1[j] & PAGE_MASK);
    }
}

fn free(root: u64) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    const l0 = l2v(root);
    for (0..512) |i| {
        if (!pteValid(l0[i]) or !isTableEntry(l0[i])) continue;
        freeL1Table(l2v(l0[i] & PAGE_MASK));
        _ = pmm_if.release(l0[i] & PAGE_MASK);
    }
    _ = pmm_if.release(root);
    return 0;
}

// --- Deep copy (copy) -------------------------------------------------------

fn copyL2Table(dst_l2: [*]u64, src_l2: [*]u64) c_int {
    for (0..512) |k| {
        if (!pteValid(src_l2[k])) continue;
        if (!isTableEntry(src_l2[k])) {
            dst_l2[k] = src_l2[k];
            continue;
        }

        var new_l3_phys: u64 = undefined;
        if (pmm_if.alloc_page(0, &new_l3_phys) != 0) return abi.ENOMEM;

        dst_l2[k] = new_l3_phys | ARM_TABLE_DESCRIPTOR;
        const src_l3 = l2v(src_l2[k] & PAGE_MASK);
        const dst_l3 = l2v(new_l3_phys);
        @memcpy(dst_l3[0..512], src_l3[0..512]);
    }
    return 0;
}

fn copyL1Table(dst_l1: [*]u64, src_l1: [*]u64) c_int {
    for (0..512) |j| {
        if (!pteValid(src_l1[j])) continue;
        if (!isTableEntry(src_l1[j])) {
            dst_l1[j] = src_l1[j];
            continue;
        }

        var new_l2_phys: u64 = undefined;
        if (pmm_if.alloc_page(0, &new_l2_phys) != 0) return abi.ENOMEM;

        dst_l1[j] = new_l2_phys | ARM_TABLE_DESCRIPTOR;
        const src_l2 = l2v(src_l1[j] & PAGE_MASK);
        const dst_l2 = l2v(new_l2_phys);
        @memset(dst_l2[0..512], 0);

        const status = copyL2Table(dst_l2, src_l2);
        if (status != 0) return status;
    }
    return 0;
}

fn copy(src_root: u64, dst_root: *u64) callconv(.c) c_int {
    if (src_root == 0) return abi.EINVAL;

    var new_l0_phys: u64 = undefined;
    if (pmm_if.alloc_page(0, &new_l0_phys) != 0) return abi.ENOMEM;

    const src_l0 = l2v(src_root);
    const dst_l0 = l2v(new_l0_phys);
    @memset(dst_l0[0..512], 0);

    for (0..512) |i| {
        if (!pteValid(src_l0[i])) continue;

        var new_l1_phys: u64 = undefined;
        if (pmm_if.alloc_page(0, &new_l1_phys) != 0) {
            _ = free(new_l0_phys);
            return abi.ENOMEM;
        }

        dst_l0[i] = new_l1_phys | ARM_TABLE_DESCRIPTOR;
        const src_l1 = l2v(src_l0[i] & PAGE_MASK);
        const dst_l1 = l2v(new_l1_phys);
        @memset(dst_l1[0..512], 0);

        const status = copyL1Table(dst_l1, src_l1);
        if (status != 0) {
            _ = free(new_l0_phys);
            return status;
        }
    }

    dst_root.* = new_l0_phys;
    return 0;
}

// --- Mapping (map) -----------------------------------------------------------

fn mapSinglePage(l0: [*]u64, vaddr: u64, paddr: u64, pg_size: u64, f: u64) c_int {
    const idx = extractIndices(vaddr);
    const flags = buildLeafFlags(f);

    const l1_phys = getOrAllocTable(&l0[idx.l0]) orelse return abi.ENOMEM;
    const l1 = l2v(l1_phys);

    if (pg_size == PS_1GB) {
        if (pteValid(l1[idx.l1])) return abi.EEXIST;
        l1[idx.l1] = (paddr & ~@as(u64, 0x3FFFFFFF)) | ((flags & ~@as(u64, 1 << 1)) | 1);
        return 0;
    }

    const l2_phys = getOrAllocTable(&l1[idx.l1]) orelse return abi.ENOMEM;
    const l2 = l2v(l2_phys);

    if (pg_size == PS_2MB) {
        if (pteValid(l2[idx.l2])) return abi.EEXIST;
        l2[idx.l2] = (paddr & ~@as(u64, 0x1FFFFF)) | ((flags & ~@as(u64, 1 << 1)) | 1);
        return 0;
    }

    const l3_phys = getOrAllocTable(&l2[idx.l2]) orelse return abi.ENOMEM;
    const l3 = l2v(l3_phys);

    if (pteValid(l3[idx.l3])) return abi.EEXIST;
    l3[idx.l3] = (paddr & PAGE_MASK) | flags | ARM_PAGE_DESCRIPTOR;
    return 0;
}

fn map(root: u64, virt: u64, phys: u64, pg_count: u64, pg_size: abi.PageSize, f: u64) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    const sz: u64 = @intFromEnum(pg_size);
    if (virt & (sz - 1) != 0 or phys & (sz - 1) != 0) return abi.EINVAL;

    // Wrapping arithmetic is intentional here: this is purely an overflow
    // *check* (if the true sum wraps past `virt`, it overflowed).
    if (virt +% (pg_count *% sz) < virt) return abi.EOVERFLOW;

    const l0 = l2v(root);
    var i: u64 = 0;
    while (i < pg_count) : (i += 1) {
        const status = mapSinglePage(l0, virt + i * sz, phys + i * sz, sz, f);
        if (status != 0) return status;
    }

    return invalidate(virt, pg_count, pg_size);
}

// --- Unmapping (unmap) -------------------------------------------------------

fn pruneIfEmpty(phys: u64, parent_entry: *u64) void {
    if (isPtEmpty(phys)) {
        _ = pmm_if.release(phys);
        parent_entry.* = 0;
    }
}

fn unmapSinglePage(l0: [*]u64, vaddr: u64, pg_size: u64) void {
    const idx = extractIndices(vaddr);
    if (!pteValid(l0[idx.l0])) return;
    const l1_phys = l0[idx.l0] & PAGE_MASK;
    const l1 = l2v(l1_phys);

    if (pg_size != PS_1GB) {
        if (!pteValid(l1[idx.l1])) return;
        const l2_phys = l1[idx.l1] & PAGE_MASK;
        const l2 = l2v(l2_phys);

        if (pg_size != PS_2MB) {
            if (!pteValid(l2[idx.l2])) return;
            const l3_phys = l2[idx.l2] & PAGE_MASK;
            const l3 = l2v(l3_phys);

            if (pg_size == PS_4KB) l3[idx.l3] = 0;

            pruneIfEmpty(l3_phys, &l2[idx.l2]);
        } else {
            l2[idx.l2] = 0;
        }

        pruneIfEmpty(l2_phys, &l1[idx.l1]);
    } else {
        l1[idx.l1] = 0;
    }

    pruneIfEmpty(l1_phys, &l0[idx.l0]);
}

fn unmap(root: u64, virt: u64, pg_count: u64, pg_size: abi.PageSize) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    const sz: u64 = @intFromEnum(pg_size);
    const l0 = l2v(root);
    var i: u64 = 0;
    while (i < pg_count) : (i += 1) {
        unmapSinglePage(l0, virt + i * sz, sz);
    }

    return invalidate(virt, pg_count, pg_size);
}

// --- Protection (protect) -----------------------------------------------------

fn protectSinglePage(l0: [*]u64, vaddr: u64, pg_size: u64, f: u64) c_int {
    const idx = extractIndices(vaddr);
    const flags = buildLeafFlags(f);

    if (!pteValid(l0[idx.l0])) return abi.EFAULT;
    const l1 = l2v(l0[idx.l0] & PAGE_MASK);

    if (pg_size == PS_1GB) {
        if (!pteValid(l1[idx.l1])) return abi.EFAULT;
        // The C driver did `phys_addr | f` here, with no valid/type bits
        // included in either operand -- `pt_protect` on a 1GB/2MB block
        // would clear the descriptor's valid bit, unmapping the very page
        // it was asked to protect. Rebuild the block descriptor properly,
        // matching how mapSinglePage constructs one.
        const phys_addr = l1[idx.l1] & PTE_ADDR_MASK;
        l1[idx.l1] = phys_addr | ((flags & ~@as(u64, 1 << 1)) | 1);
        return 0;
    }

    if (!pteValid(l1[idx.l1])) return abi.EFAULT;
    const l2 = l2v(l1[idx.l1] & PAGE_MASK);

    if (pg_size == PS_2MB) {
        if (!pteValid(l2[idx.l2])) return abi.EFAULT;
        const phys_addr = l2[idx.l2] & PTE_ADDR_MASK;
        l2[idx.l2] = phys_addr | ((flags & ~@as(u64, 1 << 1)) | 1);
        return 0;
    }

    if (!pteValid(l2[idx.l2])) return abi.EFAULT;
    const l3 = l2v(l2[idx.l2] & PAGE_MASK);

    if (!pteValid(l3[idx.l3])) return abi.EFAULT;
    const phys_addr = l3[idx.l3] & PTE_ADDR_MASK;
    l3[idx.l3] = phys_addr | flags | ARM_PAGE_DESCRIPTOR;
    return 0;
}

fn protect(root: u64, virt: u64, pg_count: u64, pg_size: abi.PageSize, f: u64) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    const sz: u64 = @intFromEnum(pg_size);
    const l0 = l2v(root);
    var i: u64 = 0;
    while (i < pg_count) : (i += 1) {
        const status = protectSinglePage(l0, virt + i * sz, sz, f);
        if (status != 0) return status;
    }

    return invalidate(virt, pg_count, pg_size);
}

// --- System utilities & core entry points -------------------------------------

fn alloc(out_root: *u64) callconv(.c) c_int {
    var root: u64 = undefined;
    if (pmm_if.alloc_page(0, &root) != 0) return abi.ENOMEM;
    @memset(l2v(root)[0..512], 0);
    out_root.* = root;
    return 0;
}

fn setUserCtx(root: u64, asid: u16) callconv(.c) c_int {
    const ttbr = (@as(u64, asid) << 48) | (root & PAGE_MASK);
    asm volatile ("msr ttbr0_el1, %[v]\nisb"
        :
        : [v] "r" (ttbr),
        : .{ .memory = true }
    );
    return 0;
}

fn setKernelCtx(root: u64, asid: u16) callconv(.c) c_int {
    const ttbr = (@as(u64, asid) << 48) | (root & PAGE_MASK);
    asm volatile ("msr ttbr1_el1, %[v]\nisb"
        :
        : [v] "r" (ttbr),
        : .{ .memory = true }
    );
    return 0;
}

fn getUserCtx(out_root: *u64) callconv(.c) c_int {
    const ttbr0 = asm volatile ("mrs %[out], ttbr0_el1"
        : [out] "=r" (-> u64),
    );
    out_root.* = ttbr0 & PAGE_MASK;
    return 0;
}

fn getKernelCtx(out_root: *u64) callconv(.c) c_int {
    const ttbr1 = asm volatile ("mrs %[out], ttbr1_el1"
        : [out] "=r" (-> u64),
    );
    out_root.* = ttbr1 & PAGE_MASK;
    return 0;
}

fn translate(root: u64, virt: u64, phys_out: *u64, flags_out: *u64) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    const idx = extractIndices(virt);
    const l0 = l2v(root);

    if (!pteValid(l0[idx.l0])) return abi.EFAULT;
    const l1 = l2v(l0[idx.l0] & PAGE_MASK);

    if (!pteValid(l1[idx.l1])) return abi.EFAULT;
    if (!isTableEntry(l1[idx.l1])) {
        const phys_base = l1[idx.l1] & ~@as(u64, 0x3FFFFFFF);
        phys_out.* = phys_base + (virt & 0x3FFFFFFF);
        flags_out.* = l1[idx.l1] & PTE_RETURN_FLAG_MASK;
        return 0;
    }

    const l2 = l2v(l1[idx.l1] & PAGE_MASK);
    if (!pteValid(l2[idx.l2])) return abi.EFAULT;
    if (!isTableEntry(l2[idx.l2])) {
        const phys_base = l2[idx.l2] & ~@as(u64, 0x1FFFFF);
        phys_out.* = phys_base + (virt & 0x1FFFFF);
        flags_out.* = l2[idx.l2] & PTE_RETURN_FLAG_MASK;
        return 0;
    }

    const l3 = l2v(l2[idx.l2] & PAGE_MASK);
    if (!pteValid(l3[idx.l3])) return abi.EFAULT;

    const phys_base = l3[idx.l3] & PAGE_MASK;
    phys_out.* = phys_base + idx.offset;
    flags_out.* = l3[idx.l3] & PTE_RETURN_FLAG_MASK;
    return 0;
}

fn flush() callconv(.c) c_int {
    asm volatile ("tlbi vmalle1is\ndsb ish\nisb" ::: .{ .memory = true });
    return 0;
}

fn invalidate(virt: u64, pg_count: u64, pg_size: abi.PageSize) callconv(.c) c_int {
    if (pg_count > TLB_BATCH_THRESHOLD) {
        asm volatile ("tlbi vmalle1is\ndsb ish\nisb" ::: .{ .memory = true });
        return 0;
    }

    const ttbr0 = asm volatile ("mrs %[out], ttbr0_el1"
        : [out] "=r" (-> u64),
    );
    const asid = (ttbr0 >> 48) & 0xFFFF;
    const sz: u64 = @intFromEnum(pg_size);

    var i: u64 = 0;
    while (i < pg_count) : (i += 1) {
        const target = virt + i * sz;
        var payload = (target >> 12) & 0x000000FFFFFFFFFF;
        payload |= asid << 48;

        if (target >= 0xFFFF800000000000) {
            asm volatile ("tlbi vae1is, %[v]"
                :
                : [v] "r" (payload),
                : .{ .memory = true }
            );
        } else {
            asm volatile ("tlbi vale1is, %[v]"
                :
                : [v] "r" (payload),
                : .{ .memory = true }
            );
        }
    }

    asm volatile ("dsb ish\nisb" ::: .{ .memory = true });
    return 0;
}

fn setMair(mair: u64) callconv(.c) c_int {
    asm volatile ("msr mair_el1, %[v]\nisb"
        :
        : [v] "r" (mair),
        : .{ .memory = true }
    );
    return 0;
}

comptime {
    abi.exportInterface("mmu", abi.Mmu, .{
        .alloc = alloc,
        .free = free,
        .copy = copy,
        .set_user_ctx = setUserCtx,
        .set_kernel_ctx = setKernelCtx,
        .get_user_ctx = getUserCtx,
        .get_kernel_ctx = getKernelCtx,
        .map = map,
        .unmap = unmap,
        .protect = protect,
        .translate = translate,
        .flush = flush,
        .invalidate = invalidate,
        .set_mair = setMair,
    });
}

// --- Unit tests --------------------------------------------------------------

fn testPageTableCreation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const TEST_COUNT = 5;
    const TEST_RANGE = 128;

    for (0..TEST_COUNT) |_| {
        var root: u64 = undefined;
        t.expectEqual(@src(), alloc(&root), 0);
        t.expectNotEqual(@src(), root, 0);

        for (0..TEST_RANGE) |i| {
            // The C original passed the raw loop counter as `phys` (not
            // page-aligned for i > 0, so pt_map would reject nearly every
            // call with EINVAL) and then didn't actually check the map
            // status -- it re-asserted `root != 0` instead, a tautology
            // left over from a copy-paste that always passed regardless
            // of whether the mapping succeeded. Map real page-aligned
            // physical addresses and check the real result.
            const status = map(root, i * 4096 + 4096, i * 4096, 1, .ps_4kb, 0);
            t.expectEqual(@src(), status, 0);
        }

        t.expectEqual(@src(), free(root), 0);
    }
    return t.result();
}

fn testTranslationAndFlags() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), alloc(&root), 0);

    const virt_addr: u64 = 0x00007FFFF0000000;
    const phys_addr: u64 = 0x10000000;
    const map_flags: u64 = abi.MMU_USER;

    t.expectEqual(@src(), map(root, virt_addr, phys_addr, 1, .ps_4kb, map_flags), 0);

    var out_phys: u64 = 0;
    var out_flags: u64 = 0;
    t.expectEqual(@src(), translate(root, virt_addr, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_addr);
    t.expectEqual(@src(), out_flags, map_flags);

    var unmapped_phys: u64 = undefined;
    var unmapped_flags: u64 = undefined;
    t.expectNotEqual(@src(), translate(root, virt_addr + 0x5000, &unmapped_phys, &unmapped_flags), 0);

    t.expectEqual(@src(), free(root), 0);
    return t.result();
}

fn testHugePages() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), alloc(&root), 0);

    const virt_2mb: u64 = 0x00007FFF00000000;
    const phys_2mb: u64 = 0x40000000;
    t.expectEqual(@src(), map(root, virt_2mb, phys_2mb, 1, .ps_2mb, abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), translate(root, virt_2mb, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_2mb);

    const virt_1gb: u64 = 0x00007FFE00000000;
    const phys_1gb: u64 = 0x80000000;
    t.expectEqual(@src(), map(root, virt_1gb, phys_1gb, 1, .ps_1gb, abi.MMU_USER), 0);

    t.expectEqual(@src(), translate(root, virt_1gb, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_1gb);

    t.expectEqual(@src(), free(root), 0);
    return t.result();
}

fn testPageProtectionUpdate() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), alloc(&root), 0);

    const virt: u64 = 0x00007FFFF5500000;
    const phys: u64 = 0x90000000;

    t.expectEqual(@src(), map(root, virt, phys, 1, .ps_4kb, abi.MMU_USER), 0);
    t.expectEqual(@src(), protect(root, virt, 1, .ps_4kb, abi.MMU_RO | abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);
    t.expectEqual(@src(), out_flags, abi.MMU_RO | abi.MMU_USER);

    t.expectEqual(@src(), free(root), 0);
    return t.result();
}

/// Regression test for the protectSinglePage bug fix: protecting a huge
/// (2MB/1GB) page must not clear its valid bit.
fn testHugePageProtectionUpdate() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), alloc(&root), 0);

    const virt: u64 = 0x00007FFD00000000;
    const phys: u64 = 0x40000000;

    t.expectEqual(@src(), map(root, virt, phys, 1, .ps_2mb, abi.MMU_USER), 0);
    t.expectEqual(@src(), protect(root, virt, 1, .ps_2mb, abi.MMU_RO | abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);
    t.expectEqual(@src(), out_flags, abi.MMU_RO | abi.MMU_USER);

    t.expectEqual(@src(), free(root), 0);
    return t.result();
}

fn testDeepCopyAndIsolation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var src_root: u64 = undefined;
    t.expectEqual(@src(), alloc(&src_root), 0);

    const virt: u64 = 0x00007FFFFCC00000;
    const phys: u64 = 0xA0000000;

    t.expectEqual(@src(), map(src_root, virt, phys, 1, .ps_4kb, abi.MMU_USER), 0);

    var dest_root: u64 = undefined;
    t.expectEqual(@src(), copy(src_root, &dest_root), 0);
    t.expectNotEqual(@src(), src_root, dest_root);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), translate(dest_root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);

    t.expectEqual(@src(), unmap(src_root, virt, 1, .ps_4kb), 0);

    // The clone is independent: still mapped even after the source is
    // unmapped.
    t.expectEqual(@src(), translate(dest_root, virt, &out_phys, &out_flags), 0);

    t.expectEqual(@src(), free(src_root), 0);
    t.expectEqual(@src(), free(dest_root), 0);
    return t.result();
}

fn testUserContextSwitchAndInvalidate() callconv(.c) i32 {
    // Validation properties uncompleted: skipped to safeguard hardware
    // runtime steps (matches the C test's TEST_SKIP()).
    return abi.TEST_SKIP;
}

comptime {
    abi.kernelTest("page_table_creation", &testPageTableCreation);
    abi.kernelTest("translation_and_flags", &testTranslationAndFlags);
    abi.kernelTest("huge_pages", &testHugePages);
    abi.kernelTest("page_protection_update", &testPageProtectionUpdate);
    abi.kernelTest("huge_page_protection_update", &testHugePageProtectionUpdate);
    abi.kernelTest("deep_copy_and_isolation", &testDeepCopyAndIsolation);
    abi.kernelTest("user_context_switch_and_invalidate", &testUserContextSwitchAndInvalidate);
}
