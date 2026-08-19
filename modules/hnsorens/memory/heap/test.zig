//! Tests for the boundary-tag heap allocator in `main.zig`, split into
//! their own file (reachable from the build via `main.zig`'s `comptime {
//! _ = @import("test.zig"); }`).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testLifecycleAndBasicMalloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);
    const initial_size: u64 = 64 * 1024;

    var heap_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapCreate(vmm_root, initial_size, &heap_opaque), 0);
    t.expectTrue(@src(), heap_opaque != null);

    var used: u64 = 0;
    var total: u64 = 0;
    t.expectEqual(@src(), main.heapGetStats(heap_opaque, &used, &total), 0);
    t.expectEqual(@src(), used, 0);
    t.expectLessThan(@src(), initial_size, total);

    var ptr1: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 256, &ptr1), 0);
    t.expectTrue(@src(), ptr1 != null);

    _ = main.heapGetStats(heap_opaque, &used, &total);
    t.expectLessOrEqual(@src(), @as(u64, 256), used);

    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr1), 0);

    _ = main.heapGetStats(heap_opaque, &used, &total);
    t.expectEqual(@src(), used, 0);

    t.expectEqual(@src(), main.heapDestroy(heap_opaque), 0);
    return t.result();
}

fn testFragmentationAndCoalescing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapCreate(vmm_root, 128 * 1024, &heap_opaque), 0);

    var p1: ?*anyopaque = null;
    var p2: ?*anyopaque = null;
    var p3: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 1024, &p1), 0);
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 1024, &p2), 0);
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 1024, &p3), 0);

    t.expectEqual(@src(), main.heapFree(heap_opaque, p1), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, p3), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, p2), 0);

    var final_used: u64 = 1;
    var total_dummy: u64 = undefined;
    _ = main.heapGetStats(heap_opaque, &final_used, &total_dummy);
    t.expectEqual(@src(), final_used, 0);

    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testReallocInPlaceAndMigration() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 128 * 1024, &heap_opaque);

    var p1: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 128, &p1), 0);

    const data1: [*]u8 = @ptrCast(p1.?);
    for (0..128) |i| data1[i] = @truncate(i);

    var p2: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapRealloc(heap_opaque, p1, 64, &p2), 0);
    t.expectEqual(@src(), @intFromPtr(p1), @intFromPtr(p2));

    const data2: [*]const u8 = @ptrCast(p2.?);
    for (0..64) |i| t.expectEqual(@src(), data2[i], @as(u8, @truncate(i)));

    var barrier: ?*anyopaque = null;
    _ = main.heapMalloc(heap_opaque, 256, &barrier);

    var p3: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapRealloc(heap_opaque, p2, 2048, &p3), 0);
    t.expectNotEqual(@src(), @intFromPtr(p2), @intFromPtr(p3));

    const data3: [*]const u8 = @ptrCast(p3.?);
    for (0..64) |i| t.expectEqual(@src(), data3[i], @as(u8, @truncate(i)));

    _ = main.heapFree(heap_opaque, barrier);
    _ = main.heapFree(heap_opaque, p3);
    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testReallocGrowIntoForwardNeighbor() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 128 * 1024, &heap_opaque);

    // p1 immediately followed by p2 in address order (first-fit on a
    // freshly created heap guarantees this). Freeing p2 leaves a free
    // block directly adjacent to p1 -- growing p1 into it should reuse
    // that space in place (realloc's "Path Beta") rather than migrating.
    var p1: ?*anyopaque = null;
    var p2: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 64, &p1), 0);
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 64, &p2), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, p2), 0);

    const data: [*]u8 = @ptrCast(p1.?);
    for (0..64) |i| data[i] = @truncate(i + 7);

    var grown: ?*anyopaque = null;
    // Bigger than p1's own block, but small enough to fit p1's block plus
    // the now-free p2 block combined.
    t.expectEqual(@src(), main.heapRealloc(heap_opaque, p1, 200, &grown), 0);
    t.expectEqual(@src(), @intFromPtr(grown), @intFromPtr(p1));

    const grown_data: [*]const u8 = @ptrCast(grown.?);
    for (0..64) |i| t.expectEqual(@src(), grown_data[i], @as(u8, @truncate(i + 7)));

    t.expectEqual(@src(), main.heapFree(heap_opaque, grown), 0);
    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testMemalignBoundaryChecks() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 512 * 1024, &heap_opaque);

    var aligned_ptr: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 64, 1023, &aligned_ptr), 0);
    t.expectTrue(@src(), aligned_ptr != null);
    t.expectEqual(@src(), @intFromPtr(aligned_ptr.?) & (64 - 1), 0);

    var page_aligned_ptr: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 4096, 8000, &page_aligned_ptr), 0);
    t.expectTrue(@src(), page_aligned_ptr != null);
    t.expectEqual(@src(), @intFromPtr(page_aligned_ptr.?) & (4096 - 1), 0);

    t.expectEqual(@src(), main.heapFree(heap_opaque, aligned_ptr), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, page_aligned_ptr), 0);

    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

/// Forces heapMemalign's "carve off leading pad" branch: a small
/// odd-sized allocation ahead of it shifts the next free block's natural
/// start so it no longer satisfies the requested alignment on its own,
/// unlike `testMemalignBoundaryChecks`, which happens to get lucky on a
/// pristine heap where the first free block is already aligned.
fn testMemalignForcesPaddingSplit() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 512 * 1024, &heap_opaque);

    var shifter: ?*anyopaque = null;
    // 16-byte allocation (heap's minimum alignment granule) shifts the
    // next free block's start by exactly one granule -- not a multiple of
    // any alignment >= 32, guaranteeing padding > 0 below.
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 16, &shifter), 0);

    var ptr: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 256, 512, &ptr), 0);
    t.expectTrue(@src(), ptr != null);
    t.expectEqual(@src(), @intFromPtr(ptr.?) & (256 - 1), 0);

    t.expectEqual(@src(), main.heapFree(heap_opaque, shifter), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr), 0);
    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testMemalignAlignmentExceedsCapacity() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 32 * 1024, &heap_opaque);

    var ptr: ?*anyopaque = null;
    // Larger than the entire heap -- must fail cleanly (ENOMEM), not wrap
    // or corrupt the free list.
    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 64, 1024 * 1024, &ptr), abi.ENOMEM);

    // The heap must still be usable afterward.
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 64, &ptr), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr), 0);

    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testDoubleFreeRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 32 * 1024, &heap_opaque);

    var ptr: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 64, &ptr), 0);
    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr), 0);

    // The block's magic is now HEAP_MAGIC_FREE, not _ALLOCATED -- a second
    // free of the same pointer must be rejected, not silently corrupt the
    // free list by double-coalescing it.
    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr), abi.EINVAL);

    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testStatsTrackMemalignUsage() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 128 * 1024, &heap_opaque);

    var used: u64 = 0;
    var total: u64 = 0;
    _ = main.heapGetStats(heap_opaque, &used, &total);
    t.expectEqual(@src(), used, 0);

    var ptr: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 128, 500, &ptr), 0);

    _ = main.heapGetStats(heap_opaque, &used, &total);
    t.expectLessOrEqual(@src(), @as(u64, 500), used);

    t.expectEqual(@src(), main.heapFree(heap_opaque, ptr), 0);
    _ = main.heapGetStats(heap_opaque, &used, &total);
    t.expectEqual(@src(), used, 0);

    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

fn testCreateAndDestroyInvalidArgs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var heap_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.heapCreate(0, 4096, &heap_opaque), abi.EINVAL);
    t.expectEqual(@src(), main.heapDestroy(null), abi.EINVAL);
    return t.result();
}

fn testSecurityAndEdgeCases() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = main.heapCreate(vmm_root, 64 * 1024, &heap_opaque);

    var out_ptr: ?*anyopaque = @ptrFromInt(0xDEADBEEF);
    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 0, &out_ptr), 0);
    t.expectTrue(@src(), out_ptr == null);

    t.expectEqual(@src(), main.heapFree(heap_opaque, null), 0);

    t.expectEqual(@src(), main.heapMemalign(heap_opaque, 31, 128, &out_ptr), abi.EINVAL);

    t.expectEqual(@src(), main.heapMalloc(heap_opaque, 1024 * 1024, &out_ptr), abi.ENOMEM);

    var corrupted_ptr: ?*anyopaque = null;
    _ = main.heapMalloc(heap_opaque, 64, &corrupted_ptr);

    const block: *main.HeapBlock = @ptrFromInt(@intFromPtr(corrupted_ptr.?) - main.BLOCK_HEADER_SIZE);
    const original_magic = block.magic;
    block.magic = 0xDEADC0DE;

    t.expectEqual(@src(), main.heapFree(heap_opaque, corrupted_ptr), abi.EINVAL);

    block.magic = original_magic;
    _ = main.heapFree(heap_opaque, corrupted_ptr);
    _ = main.heapDestroy(heap_opaque);
    return t.result();
}

comptime {
    abi.kernelTest("lifecycle_and_basic_malloc", &testLifecycleAndBasicMalloc);
    abi.kernelTest("fragmentation_and_coalescing", &testFragmentationAndCoalescing);
    abi.kernelTest("realloc_in_place_and_migration", &testReallocInPlaceAndMigration);
    abi.kernelTest("realloc_grow_into_forward_neighbor", &testReallocGrowIntoForwardNeighbor);
    abi.kernelTest("memalign_boundary_checks", &testMemalignBoundaryChecks);
    abi.kernelTest("memalign_forces_padding_split", &testMemalignForcesPaddingSplit);
    abi.kernelTest("memalign_alignment_exceeds_capacity", &testMemalignAlignmentExceedsCapacity);
    abi.kernelTest("double_free_rejected", &testDoubleFreeRejected);
    abi.kernelTest("stats_track_memalign_usage", &testStatsTrackMemalignUsage);
    abi.kernelTest("create_and_destroy_invalid_args", &testCreateAndDestroyInvalidArgs);
    abi.kernelTest("security_and_edge_cases", &testSecurityAndEdgeCases);
}
