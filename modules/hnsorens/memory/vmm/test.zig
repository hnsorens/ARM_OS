//! Tests for the virtual memory manager in `main.zig`, split into their
//! own file (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! `activate()` is deliberately never exercised for real here, for the
//! same reason `mmu`'s user-context-switch test is skipped: it writes
//! TTBR0_EL1, which would immediately invalidate the identity mapping
//! this very test code and the UART depend on.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testSpaceLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = 0;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);
    t.expectNotEqual(@src(), root, 0);
    t.expectEqual(@src(), main.spaceDestroy(root), 0);

    // The C original also checked `vmm_space_create(NULL)` here; that
    // invariant (out_table_root is never null) is enforced at the type
    // level in this port (a non-optional `*u64`), so there's no runtime
    // path left to exercise -- constructing a null value for it would
    // itself trip Zig's own safety checks.
    t.expectEqual(@src(), main.spaceDestroy(0), abi.EINVAL);
    return t.result();
}

fn testAllocationAndQuery() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const requested_sz: u64 = 4096 * 3;
    t.expectEqual(@src(), main.allocate(root, &vaddr, requested_sz, abi.MMU_USER, .data), 0);
    t.expectNotEqual(@src(), vaddr, 0);

    var info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr + 4096, &info), 0);
    t.expectEqual(@src(), info.base, vaddr);
    t.expectEqual(@src(), info.size, 4096 * 3);
    t.expectEqual(@src(), info.region_type, .data);
    t.expectEqual(@src(), info.is_paged, true);

    t.expectNotEqual(@src(), main.query(root, vaddr + 4096 * 10, &info), 0);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testStackGuardLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var stack_vaddr: u64 = 0x00007FFFF0000000;
    const stack_sz: u64 = 4096 * 4;
    const guard_vaddr = stack_vaddr - 4096;

    t.expectEqual(@src(), main.reserve(root, guard_vaddr, 4096), 0);
    t.expectEqual(@src(), main.allocate(root, &stack_vaddr, stack_sz, abi.MMU_USER, .stack), 0);

    var guard_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, guard_vaddr, &guard_info), 0);
    t.expectEqual(@src(), guard_info.region_type, .guard);
    t.expectEqual(@src(), guard_info.size, 4096);
    t.expectEqual(@src(), guard_info.is_paged, false);

    t.expectEqual(@src(), main.free(root, stack_vaddr, stack_sz), 0);
    t.expectEqual(@src(), main.free(root, guard_vaddr, 4096), 0);

    t.expectNotEqual(@src(), main.query(root, guard_vaddr, &guard_info), 0);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testFreeSubrangeSplitting() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const total_sz: u64 = 4096 * 5;
    t.expectEqual(@src(), main.allocate(root, &vaddr, total_sz, abi.MMU_USER, .data), 0);

    const punch_vaddr = vaddr + 4096 * 2;
    t.expectEqual(@src(), main.free(root, punch_vaddr, 4096), 0);

    var left_chunk: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &left_chunk), 0);
    t.expectEqual(@src(), left_chunk.size, 4096 * 2);

    var right_chunk: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr + 4096 * 3, &right_chunk), 0);
    t.expectEqual(@src(), right_chunk.base, vaddr + 4096 * 3);
    t.expectEqual(@src(), right_chunk.size, 4096 * 2);

    var middle_hole: abi.VmmRegionInfo = undefined;
    t.expectNotEqual(@src(), main.query(root, punch_vaddr, &middle_hole), 0);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testDynamicResizing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    t.expectEqual(@src(), main.allocate(root, &vaddr, 4096 * 2, abi.MMU_USER, .data), 0);

    t.expectEqual(@src(), main.resize(root, vaddr, 4096 * 2, 4096 * 5), 0);
    var expand_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &expand_info), 0);
    t.expectEqual(@src(), expand_info.size, 4096 * 5);

    t.expectEqual(@src(), main.resize(root, vaddr, 4096 * 5, 4096 * 1), 0);
    var shrink_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &shrink_info), 0);
    t.expectEqual(@src(), shrink_info.size, 4096 * 1);

    var blocker_vaddr = vaddr + 4096;
    t.expectEqual(@src(), main.allocate(root, &blocker_vaddr, 4096, abi.MMU_USER, .data), 0);

    t.expectEqual(@src(), main.resize(root, vaddr, 4096 * 1, 4096 * 3), abi.ENOMEM);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testProtectFragmentation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const total_sz: u64 = 4096 * 3;
    t.expectEqual(@src(), main.allocate(root, &vaddr, total_sz, abi.MMU_USER, .data), 0);

    const mid_vaddr = vaddr + 4096;
    t.expectEqual(@src(), main.protect(root, mid_vaddr, 4096, abi.MMU_RO | abi.MMU_USER), 0);

    var left: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &left), 0);
    t.expectEqual(@src(), left.size, 4096);
    t.expectEqual(@src(), left.flags, abi.MMU_USER);

    var mid: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, mid_vaddr, &mid), 0);
    t.expectEqual(@src(), mid.size, 4096);
    t.expectEqual(@src(), mid.flags, abi.MMU_RO | abi.MMU_USER);

    var right: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr + 4096 * 2, &right), 0);
    t.expectEqual(@src(), right.size, 4096);
    t.expectEqual(@src(), right.flags, abi.MMU_USER);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testExhaustionLimits() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const MAX_VMA_POOL_SIZE = 512;

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var allocated: [MAX_VMA_POOL_SIZE + 5]u64 = undefined;
    var allocation_count: usize = 0;

    for (0..MAX_VMA_POOL_SIZE + 2) |_| {
        var hint: u64 = 0;
        const status = main.allocate(root, &hint, 4096, 0x713, .data);
        if (status == 0) {
            allocated[allocation_count] = hint;
            allocation_count += 1;
        } else {
            t.expectEqual(@src(), status, abi.ENOMEM);
            break;
        }
    }

    for (0..allocation_count) |i| {
        t.expectEqual(@src(), main.free(root, allocated[i], 4096), 0);
    }

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testMapExternalMmio() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    const v: u64 = 0x00007FFFF0500000;
    const p: u64 = 0x09000000; // PL011 UART's real physical base -- just an
    // opaque identifier here, never dereferenced through this mapping.
    t.expectEqual(@src(), main.mapExternal(root, v, p, 4096, abi.MMU_USER), 0);

    var info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, v, &info), 0);
    t.expectEqual(@src(), info.region_type, .mmio);
    t.expectEqual(@src(), info.is_paged, false);
    t.expectEqual(@src(), info.size, 4096);

    // Re-mapping the same range must be rejected, not silently duplicated.
    t.expectEqual(@src(), main.mapExternal(root, v, p, 4096, abi.MMU_USER), abi.EEXIST);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testSyncAfterMapping() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0600000;
    // Deliberately not page-aligned, to exercise sync()'s round-up page
    // count computation rather than a suspiciously tidy multiple.
    const sz: u64 = 4096 * 3;
    t.expectEqual(@src(), main.allocate(root, &vaddr, sz, abi.MMU_USER, .data), 0);

    t.expectEqual(@src(), main.sync(root, vaddr, 4096 + 1), 0);
    t.expectEqual(@src(), main.sync(root, vaddr, sz), 0);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testReserveConflictRejection() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    const guard_vaddr: u64 = 0x00007FFFF0700000;
    t.expectEqual(@src(), main.reserve(root, guard_vaddr, 4096), 0);

    // Exact re-reservation and an overlapping-but-offset reservation must
    // both be rejected.
    t.expectEqual(@src(), main.reserve(root, guard_vaddr, 4096), abi.EEXIST);
    t.expectEqual(@src(), main.reserve(root, guard_vaddr - 4096, 4096 * 2), abi.EEXIST);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testProtectLeftEdgeSplit() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0800000;
    t.expectEqual(@src(), main.allocate(root, &vaddr, 4096 * 3, abi.MMU_USER, .data), 0);

    // Protect exactly the leading page: vaddr == vma.base, sz < vma.size.
    t.expectEqual(@src(), main.protect(root, vaddr, 4096, abi.MMU_RO | abi.MMU_USER), 0);

    var left: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &left), 0);
    t.expectEqual(@src(), left.size, 4096);
    t.expectEqual(@src(), left.flags, abi.MMU_RO | abi.MMU_USER);

    var rest: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr + 4096, &rest), 0);
    t.expectEqual(@src(), rest.base, vaddr + 4096);
    t.expectEqual(@src(), rest.size, 4096 * 2);
    t.expectEqual(@src(), rest.flags, abi.MMU_USER);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testProtectRightEdgeSplit() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0900000;
    t.expectEqual(@src(), main.allocate(root, &vaddr, 4096 * 3, abi.MMU_USER, .data), 0);

    // Protect exactly the trailing page: (vaddr+sz) == vma.base+vma.size.
    const tail_vaddr = vaddr + 4096 * 2;
    t.expectEqual(@src(), main.protect(root, tail_vaddr, 4096, abi.MMU_RO | abi.MMU_USER), 0);

    var head: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, vaddr, &head), 0);
    t.expectEqual(@src(), head.base, vaddr);
    t.expectEqual(@src(), head.size, 4096 * 2);
    t.expectEqual(@src(), head.flags, abi.MMU_USER);

    var tail: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), main.query(root, tail_vaddr, &tail), 0);
    t.expectEqual(@src(), tail.size, 4096);
    t.expectEqual(@src(), tail.flags, abi.MMU_RO | abi.MMU_USER);

    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

fn testFreeInvalidRangeRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), main.spaceCreate(&root), 0);

    // Nothing has ever been allocated in this space -- freeing any range
    // here must report EINVAL, not silently succeed or fault.
    t.expectEqual(@src(), main.free(root, 0x00007FFFF0A00000, 4096), abi.EINVAL);

    var vaddr: u64 = 0x00007FFFF0B00000;
    t.expectEqual(@src(), main.allocate(root, &vaddr, 4096 * 2, abi.MMU_USER, .data), 0);

    // A range that starts inside the VMA but runs past its end.
    t.expectEqual(@src(), main.free(root, vaddr, 4096 * 3), abi.EINVAL);

    t.expectEqual(@src(), main.free(root, vaddr, 4096 * 2), 0);
    t.expectEqual(@src(), main.spaceDestroy(root), 0);
    return t.result();
}

comptime {
    abi.kernelTest("space_lifecycle", &testSpaceLifecycle);
    abi.kernelTest("allocation_and_query", &testAllocationAndQuery);
    abi.kernelTest("stack_guard_lifecycle", &testStackGuardLifecycle);
    abi.kernelTest("free_subrange_splitting", &testFreeSubrangeSplitting);
    abi.kernelTest("dynamic_resizing", &testDynamicResizing);
    abi.kernelTest("protect_fragmentation", &testProtectFragmentation);
    abi.kernelTest("exhaustion_limits", &testExhaustionLimits);
    abi.kernelTest("map_external_mmio", &testMapExternalMmio);
    abi.kernelTest("sync_after_mapping", &testSyncAfterMapping);
    abi.kernelTest("reserve_conflict_rejection", &testReserveConflictRejection);
    abi.kernelTest("protect_left_edge_split", &testProtectLeftEdgeSplit);
    abi.kernelTest("protect_right_edge_split", &testProtectRightEdgeSplit);
    abi.kernelTest("free_invalid_range_rejected", &testFreeInvalidRangeRejected);
}
