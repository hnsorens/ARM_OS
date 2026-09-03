//! Tests for the AArch64 page table engine in `main.zig`, split into their
//! own file (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testPageTableCreation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const TEST_COUNT = 5;
    const TEST_RANGE = 128;

    for (0..TEST_COUNT) |_| {
        var root: u64 = undefined;
        t.expectEqual(@src(), main.alloc(&root), 0);
        t.expectNotEqual(@src(), root, 0);

        for (0..TEST_RANGE) |i| {
            // The C original passed the raw loop counter as `phys` (not
            // page-aligned for i > 0, so pt_map would reject nearly every
            // call with EINVAL) and then didn't actually check the map
            // status -- it re-asserted `root != 0` instead, a tautology
            // left over from a copy-paste that always passed regardless
            // of whether the mapping succeeded. Map real page-aligned
            // physical addresses and check the real result.
            const status = main.map(root, i * 4096 + 4096, i * 4096, 1, .ps_4kb, 0);
            t.expectEqual(@src(), status, 0);
        }

        t.expectEqual(@src(), main.free(root), 0);
    }
    return t.result();
}

fn testTranslationAndFlags() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt_addr: u64 = 0x00007FFFF0000000;
    const phys_addr: u64 = 0x10000000;
    const map_flags: u64 = abi.MMU_USER;

    t.expectEqual(@src(), main.map(root, virt_addr, phys_addr, 1, .ps_4kb, map_flags), 0);

    var out_phys: u64 = 0;
    var out_flags: u64 = 0;
    t.expectEqual(@src(), main.translate(root, virt_addr, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_addr);
    t.expectEqual(@src(), out_flags, map_flags);

    var unmapped_phys: u64 = undefined;
    var unmapped_flags: u64 = undefined;
    t.expectNotEqual(@src(), main.translate(root, virt_addr + 0x5000, &unmapped_phys, &unmapped_flags), 0);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testHugePages() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt_2mb: u64 = 0x00007FFF00000000;
    const phys_2mb: u64 = 0x40000000;
    t.expectEqual(@src(), main.map(root, virt_2mb, phys_2mb, 1, .ps_2mb, abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt_2mb, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_2mb);

    const virt_1gb: u64 = 0x00007FFE00000000;
    const phys_1gb: u64 = 0x80000000;
    t.expectEqual(@src(), main.map(root, virt_1gb, phys_1gb, 1, .ps_1gb, abi.MMU_USER), 0);

    t.expectEqual(@src(), main.translate(root, virt_1gb, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_1gb);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testPageProtectionUpdate() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt: u64 = 0x00007FFFF5500000;
    const phys: u64 = 0x90000000;

    t.expectEqual(@src(), main.map(root, virt, phys, 1, .ps_4kb, abi.MMU_USER), 0);
    t.expectEqual(@src(), main.protect(root, virt, 1, .ps_4kb, abi.MMU_RO | abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);
    t.expectEqual(@src(), out_flags, abi.MMU_RO | abi.MMU_USER);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

/// Regression test for the protectSinglePage bug fix: protecting a huge
/// (2MB/1GB) page must not clear its valid bit.
fn testHugePageProtectionUpdate() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt: u64 = 0x00007FFD00000000;
    const phys: u64 = 0x40000000;

    t.expectEqual(@src(), main.map(root, virt, phys, 1, .ps_2mb, abi.MMU_USER), 0);
    t.expectEqual(@src(), main.protect(root, virt, 1, .ps_2mb, abi.MMU_RO | abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);
    t.expectEqual(@src(), out_flags, abi.MMU_RO | abi.MMU_USER);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testDeepCopyAndIsolation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var src_root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&src_root), 0);

    const virt: u64 = 0x00007FFFFCC00000;
    const phys: u64 = 0xA0000000;

    t.expectEqual(@src(), main.map(src_root, virt, phys, 1, .ps_4kb, abi.MMU_USER), 0);

    var dest_root: u64 = undefined;
    t.expectEqual(@src(), main.copy(src_root, &dest_root), 0);
    t.expectNotEqual(@src(), src_root, dest_root);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(dest_root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys);

    t.expectEqual(@src(), main.unmap(src_root, virt, 1, .ps_4kb), 0);

    // The clone is independent: still mapped even after the source is
    // unmapped.
    t.expectEqual(@src(), main.translate(dest_root, virt, &out_phys, &out_flags), 0);

    t.expectEqual(@src(), main.free(src_root), 0);
    t.expectEqual(@src(), main.free(dest_root), 0);
    return t.result();
}

fn testUserContextSwitchAndInvalidate() callconv(.c) i32 {
    // Deliberately not exercised for real: TTBR0/TTBR1 govern the address
    // space this very test function's own code and the UART's identity
    // mapping live under. Swapping either to a freshly-allocated empty
    // table would fault on the next instruction fetch or the next MMIO
    // access, before a restore could ever run -- there's no way to
    // exercise this safely from inside a running module. Matches the C
    // test's TEST_SKIP().
    return abi.TEST_SKIP;
}

fn testUnmapClearsTranslation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt: u64 = 0x00007FFFF6600000;
    const phys: u64 = 0xB0000000;
    t.expectEqual(@src(), main.map(root, virt, phys, 1, .ps_4kb, abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);

    t.expectEqual(@src(), main.unmap(root, virt, 1, .ps_4kb), 0);
    t.expectNotEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testMultiPageMapUnmap() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt_base: u64 = 0x00007FFFF7700000;
    const phys_base: u64 = 0xC0000000;
    const count: u64 = 16;

    t.expectEqual(@src(), main.map(root, virt_base, phys_base, count, .ps_4kb, abi.MMU_USER), 0);

    for (0..count) |i| {
        var out_phys: u64 = undefined;
        var out_flags: u64 = undefined;
        t.expectEqual(@src(), main.translate(root, virt_base + i * 4096, &out_phys, &out_flags), 0);
        t.expectEqual(@src(), out_phys, phys_base + i * 4096);
    }

    t.expectEqual(@src(), main.unmap(root, virt_base, count, .ps_4kb), 0);

    for (0..count) |i| {
        var out_phys: u64 = undefined;
        var out_flags: u64 = undefined;
        t.expectNotEqual(@src(), main.translate(root, virt_base + i * 4096, &out_phys, &out_flags), 0);
    }

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

/// map()/unmap() route through invalidate(), which switches from a
/// per-page TLBI loop to a single full-ASID `tlbi vmalle1is` once
/// `pg_count` exceeds `TLB_BATCH_THRESHOLD` -- exercise that branch
/// explicitly rather than only ever mapping a handful of pages at a time.
fn testTlbBatchInvalidationPath() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt_base: u64 = 0x00007FFFF8800000;
    const phys_base: u64 = 0xD0000000;
    const count: u64 = main.TLB_BATCH_THRESHOLD + 8;

    t.expectEqual(@src(), main.map(root, virt_base, phys_base, count, .ps_4kb, abi.MMU_USER), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt_base, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_base);
    t.expectEqual(@src(), main.translate(root, virt_base + (count - 1) * 4096, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, phys_base + (count - 1) * 4096);

    t.expectEqual(@src(), main.unmap(root, virt_base, count, .ps_4kb), 0);
    t.expectNotEqual(@src(), main.translate(root, virt_base, &out_phys, &out_flags), 0);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testDoubleMapSameAddressRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt: u64 = 0x00007FFFF9900000;
    t.expectEqual(@src(), main.map(root, virt, 0xE0000000, 1, .ps_4kb, abi.MMU_USER), 0);
    t.expectEqual(@src(), main.map(root, virt, 0xE1000000, 1, .ps_4kb, abi.MMU_USER), abi.EEXIST);

    // The first mapping must be untouched by the rejected second attempt.
    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_phys, 0xE0000000);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

fn testCombinedFlagsRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.alloc(&root), 0);

    const virt: u64 = 0x00007FFFFAA00000;
    const combined = abi.MMU_RO | abi.MMU_NO_EXEC | abi.MMU_NOCACHE | abi.MMU_USER;
    t.expectEqual(@src(), main.map(root, virt, 0xF0000000, 1, .ps_4kb, combined), 0);

    var out_phys: u64 = undefined;
    var out_flags: u64 = undefined;
    t.expectEqual(@src(), main.translate(root, virt, &out_phys, &out_flags), 0);
    t.expectEqual(@src(), out_flags, combined);

    t.expectEqual(@src(), main.free(root), 0);
    return t.result();
}

const pmm_if = main.pmm_if;

// fork() gives the child independent copies of every leaf page, while
// still sharing the kernel-identity blocks; free_all reclaims everything
// the fork allocated.
fn testForkCopiesLeafPages() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var src: u64 = 0;
    t.expectEqual(@src(), main.alloc(&src), 0);

    var page: u64 = 0;
    t.expectEqual(@src(), pmm_if.alloc_page(0, &page), 0);
    const pv: [*]u8 = @ptrFromInt(page + abi.HHDM_OFFSET);
    for (0..4096) |i| pv[i] = @truncate(i * 7 + 1);

    const vaddr: u64 = 0x1_0000_0000; // 4 GiB
    t.expectEqual(@src(), main.map(src, vaddr, page, 1, .ps_4kb, abi.MMU_USER), 0);

    var child: u64 = 0;
    t.expectEqual(@src(), main.fork(src, &child), 0);

    var cphys: u64 = 0;
    var cflags: u64 = 0;
    t.expectEqual(@src(), main.translate(child, vaddr, &cphys, &cflags), 0);
    t.expectNotEqual(@src(), cphys, page); // a different physical frame

    const cv: [*]u8 = @ptrFromInt(cphys + abi.HHDM_OFFSET);
    var same = true;
    for (0..4096) |i| {
        if (cv[i] != @as(u8, @truncate(i * 7 + 1))) same = false;
    }
    t.expectTrue(@src(), same); // contents duplicated

    cv[0] = 0xEE; // mutate the child
    t.expectEqual(@src(), pv[0], @as(u8, 1)); // parent frame untouched

    t.expectEqual(@src(), main.freeAll(child), 0);
    t.expectEqual(@src(), main.free(src), 0);
    t.expectEqual(@src(), pmm_if.release(page), 0);
    return t.result();
}

fn testForkFreeAllBalancesPmm() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const before = pmm_if.get_free_memory();

    var src: u64 = 0;
    t.expectEqual(@src(), main.alloc(&src), 0);
    var page: u64 = 0;
    t.expectEqual(@src(), pmm_if.alloc_page(0, &page), 0);
    t.expectEqual(@src(), main.map(src, 0x2_0000_0000, page, 1, .ps_4kb, abi.MMU_USER), 0);

    var child: u64 = 0;
    t.expectEqual(@src(), main.fork(src, &child), 0);
    t.expectLessThan(@src(), pmm_if.get_free_memory(), before);

    t.expectEqual(@src(), main.freeAll(child), 0); // tables + the copied leaf frame
    t.expectEqual(@src(), main.free(src), 0);
    t.expectEqual(@src(), pmm_if.release(page), 0);
    t.expectEqual(@src(), pmm_if.get_free_memory(), before);
    return t.result();
}

comptime {
    abi.kernelTest("fork_copies_leaf_pages", &testForkCopiesLeafPages);
    abi.kernelTest("fork_free_all_balances_pmm", &testForkFreeAllBalancesPmm);
    abi.kernelTest("page_table_creation", &testPageTableCreation);
    abi.kernelTest("translation_and_flags", &testTranslationAndFlags);
    abi.kernelTest("huge_pages", &testHugePages);
    abi.kernelTest("page_protection_update", &testPageProtectionUpdate);
    abi.kernelTest("huge_page_protection_update", &testHugePageProtectionUpdate);
    abi.kernelTest("deep_copy_and_isolation", &testDeepCopyAndIsolation);
    abi.kernelTest("user_context_switch_and_invalidate", &testUserContextSwitchAndInvalidate);
    abi.kernelTest("unmap_clears_translation", &testUnmapClearsTranslation);
    abi.kernelTest("multi_page_map_unmap", &testMultiPageMapUnmap);
    abi.kernelTest("tlb_batch_invalidation_path", &testTlbBatchInvalidationPath);
    abi.kernelTest("double_map_same_address_rejected", &testDoubleMapSameAddressRejected);
    abi.kernelTest("combined_flags_round_trip", &testCombinedFlagsRoundTrip);
}
