//! Tests for the slab allocator in `main.zig`, split into their own file
//! (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testCacheLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 32, 8, &cache_opaque), 0);
    t.expectTrue(@src(), cache_opaque != null);
    {
        const cache = main.asCache(cache_opaque).?;
        t.expectEqual(@src(), cache.is_allocated, true);
        t.expectEqual(@src(), cache.obj_size, 32);
    }

    // Note: unlike the C original (which leaked the cache descriptor's own
    // page on every destroy), this port actually unmaps it -- so the
    // handle is genuinely invalid afterward and must not be dereferenced
    // to check `is_allocated`, unlike the C test this was ported from.
    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);

    var cache2: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 0, 8, &cache2), abi.EINVAL);
    t.expectEqual(@src(), main.createCache(vmm_root, 32, 0, &cache2), abi.EINVAL);
    t.expectEqual(@src(), main.createCache(vmm_root, main.SLAB_PAGE_SIZE + 8, 8, &cache2), abi.EINVAL);

    t.expectEqual(@src(), main.destroyCache(null), abi.EINVAL);

    var fake_cache: main.KSlabCache = .{ .is_allocated = false };
    t.expectEqual(@src(), main.destroyCache(@ptrCast(&fake_cache)), abi.EINVAL);
    return t.result();
}

fn testBasicAllocationAndFree() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 64, 16, &cache_opaque), 0);

    var obj1: ?*anyopaque = null;
    var obj2: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_opaque, &obj1), 0);
    t.expectTrue(@src(), obj1 != null);
    t.expectEqual(@src(), main.alloc(cache_opaque, &obj2), 0);
    t.expectTrue(@src(), obj2 != null);
    t.expectNotEqual(@src(), @intFromPtr(obj1), @intFromPtr(obj2));

    t.expectEqual(@src(), @intFromPtr(obj1.?) % 16, 0);
    t.expectEqual(@src(), @intFromPtr(obj2.?) % 16, 0);

    t.expectEqual(@src(), main.free(cache_opaque, obj1), 0);
    t.expectEqual(@src(), main.free(cache_opaque, obj2), 0);

    var obj3: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_opaque, &obj3), 0);
    // LIFO recovery: the most recently freed slot comes back first.
    t.expectEqual(@src(), @intFromPtr(obj3), @intFromPtr(obj2));

    t.expectEqual(@src(), main.free(cache_opaque, obj3), 0);
    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testSlabStateTransitions() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 1024, 8, &cache_opaque), 0);
    const cache = main.asCache(cache_opaque).?;

    const max_slots: usize = @intCast(cache.slots_per_slab);
    var objects: [64]?*anyopaque = undefined;

    cache.slabs_empty = null;
    cache.slabs_partial = null;
    cache.slabs_full = null;

    t.expectEqual(@src(), main.alloc(cache_opaque, &objects[0]), 0);
    t.expectTrue(@src(), cache.slabs_partial != null);
    t.expectTrue(@src(), cache.slabs_full == null);

    for (1..max_slots) |i| {
        t.expectEqual(@src(), main.alloc(cache_opaque, &objects[i]), 0);
    }

    t.expectTrue(@src(), cache.slabs_partial == null);
    t.expectTrue(@src(), cache.slabs_full != null);

    t.expectEqual(@src(), main.alloc(cache_opaque, &objects[max_slots]), 0);
    t.expectTrue(@src(), cache.slabs_partial != null);
    t.expectTrue(@src(), cache.slabs_full != null);

    t.expectEqual(@src(), main.free(cache_opaque, objects[0]), 0);
    for (1..max_slots) |i| {
        t.expectEqual(@src(), main.free(cache_opaque, objects[i]), 0);
    }

    t.expectTrue(@src(), cache.slabs_empty != null);

    t.expectEqual(@src(), main.free(cache_opaque, objects[max_slots]), 0);
    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testShrinkAndReclaim() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 256, 8, &cache_opaque), 0);
    const cache = main.asCache(cache_opaque).?;

    var obj: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_opaque, &obj), 0);
    t.expectEqual(@src(), main.free(cache_opaque, obj), 0);
    t.expectTrue(@src(), cache.slabs_empty != null);

    t.expectEqual(@src(), main.shrink(cache_opaque), 0);
    t.expectTrue(@src(), cache.slabs_empty == null);

    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testShrinkNoOpWhenNoEmptySlabs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 128, 8, &cache_opaque), 0);
    const cache = main.asCache(cache_opaque).?;

    var obj: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_opaque, &obj), 0);
    // Never freed -- the slab holding `obj` is partial/full, never empty.
    t.expectTrue(@src(), cache.slabs_empty == null);

    t.expectEqual(@src(), main.shrink(cache_opaque), 0);
    // A no-op shrink must not disturb the still-live allocation.
    t.expectTrue(@src(), cache.slabs_empty == null);
    t.expectTrue(@src(), (cache.slabs_partial != null) or (cache.slabs_full != null));

    t.expectEqual(@src(), main.free(cache_opaque, obj), 0);
    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testExhaustionAndBoundaries() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 2048, 8, &cache_opaque), 0);

    const DESCRIPTOR_BOUNDS = 135;
    var tracked: [DESCRIPTOR_BOUNDS]?*anyopaque = undefined;
    var successfully_allocated: usize = 0;

    for (0..DESCRIPTOR_BOUNDS) |i| {
        const status = main.alloc(cache_opaque, &tracked[i]);
        if (status == 0) {
            successfully_allocated += 1;
        } else {
            t.expectEqual(@src(), status, abi.ENOMEM);
            break;
        }
    }

    for (0..successfully_allocated) |i| {
        t.expectEqual(@src(), main.free(cache_opaque, tracked[i]), 0);
    }

    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testRobustnessEdgeCases() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 128, 8, &cache_opaque), 0);

    var dummy_out: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(null, &dummy_out), abi.EINVAL);
    // C's counterpart also checked `alloc(cache, NULL)` rejection, but
    // `out_obj` is `*?*anyopaque` (non-optional pointer) in Zig -- there is
    // no NULL to pass, so that case is untestable here (same situation as
    // the dropped NULL case in the vmm test port).

    const foreign_address: u64 = 0xDEADBEEF0000;
    t.expectEqual(@src(), main.free(cache_opaque, @ptrFromInt(foreign_address)), abi.EINVAL);
    t.expectEqual(@src(), main.free(null, @ptrFromInt(foreign_address)), abi.EINVAL);

    var small_cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 1, 1, &small_cache_opaque), 0);
    const small_cache = main.asCache(small_cache_opaque).?;
    t.expectLessOrEqual(@src(), @as(u64, @sizeOf(u32)), small_cache.obj_size);

    t.expectEqual(@src(), main.destroyCache(small_cache_opaque), 0);
    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

fn testMultipleCachesAreIndependent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_a: ?*anyopaque = null;
    var cache_b: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 48, 8, &cache_a), 0);
    t.expectEqual(@src(), main.createCache(vmm_root, 96, 16, &cache_b), 0);

    var a1: ?*anyopaque = null;
    var b1: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_a, &a1), 0);
    t.expectEqual(@src(), main.alloc(cache_b, &b1), 0);

    // Freeing from A must not disturb B's live allocation, and vice
    // versa -- each cache's free/partial/full lists are entirely separate.
    t.expectEqual(@src(), main.free(cache_a, a1), 0);
    var a2: ?*anyopaque = null;
    t.expectEqual(@src(), main.alloc(cache_a, &a2), 0);
    t.expectEqual(@src(), @intFromPtr(a2), @intFromPtr(a1));

    t.expectEqual(@src(), main.free(cache_b, b1), 0);
    t.expectEqual(@src(), main.destroyCache(cache_a), 0);
    t.expectEqual(@src(), main.destroyCache(cache_b), 0);
    return t.result();
}

fn testLargeAlignmentSlotComputation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    // A small object with a much larger alignment requirement -- the
    // payload start must still land on that alignment, and every
    // subsequent slot (spaced by obj_size, which already absorbed the
    // alignment) must too.
    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 8, 128, &cache_opaque), 0);
    const cache = main.asCache(cache_opaque).?;
    t.expectTrue(@src(), cache.slots_per_slab > 0);

    var objs: [8]?*anyopaque = undefined;
    for (0..8) |i| {
        t.expectEqual(@src(), main.alloc(cache_opaque, &objs[i]), 0);
        t.expectEqual(@src(), @intFromPtr(objs[i].?) % 128, 0);
    }
    for (0..8) |i| {
        t.expectEqual(@src(), main.free(cache_opaque, objs[i]), 0);
    }

    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

/// Regression test for the destroy_cache leak fix: the C original never
/// released the cache descriptor's own page, so this port's fix (actually
/// unmapping it) is verified here by confirming the mapping is genuinely
/// gone afterward, not merely by trusting the return code.
fn testDestroyCacheActuallyUnmapsDescriptor() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 64, 8, &cache_opaque), 0);
    const cache_addr = @intFromPtr(cache_opaque.?);

    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);

    var phys: u64 = undefined;
    var flags: u64 = undefined;
    t.expectNotEqual(@src(), main.mmu_if.translate(vmm_root, cache_addr, &phys, &flags), 0);
    return t.result();
}

fn testAllocFreeInterleavedManyObjects() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = main.mmu_if.get_kernel_ctx(&vmm_root);

    var cache_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), main.createCache(vmm_root, 32, 8, &cache_opaque), 0);

    var live: [40]?*anyopaque = [_]?*anyopaque{null} ** 40;

    // Interleave: allocate two, free one, repeating -- exercises the
    // partial/full/empty queue transitions in a non-monotonic order
    // rather than a clean alloc-everything-then-free-everything pass.
    var next_slot: usize = 0;
    for (0..40) |i| {
        if (next_slot < 40) {
            t.expectEqual(@src(), main.alloc(cache_opaque, &live[next_slot]), 0);
            next_slot += 1;
        }
        if (i % 3 == 1 and next_slot > 0) {
            next_slot -= 1;
            t.expectEqual(@src(), main.free(cache_opaque, live[next_slot]), 0);
            live[next_slot] = null;
        }
    }

    for (0..next_slot) |i| {
        if (live[i]) |obj| t.expectEqual(@src(), main.free(cache_opaque, obj), 0);
    }

    t.expectEqual(@src(), main.destroyCache(cache_opaque), 0);
    return t.result();
}

comptime {
    abi.kernelTest("cache_lifecycle", &testCacheLifecycle);
    abi.kernelTest("basic_allocation_and_free", &testBasicAllocationAndFree);
    abi.kernelTest("slab_state_transitions", &testSlabStateTransitions);
    abi.kernelTest("shrink_and_reclaim", &testShrinkAndReclaim);
    abi.kernelTest("shrink_no_op_when_no_empty_slabs", &testShrinkNoOpWhenNoEmptySlabs);
    abi.kernelTest("exhaustion_and_boundaries", &testExhaustionAndBoundaries);
    abi.kernelTest("robustness_edge_cases", &testRobustnessEdgeCases);
    abi.kernelTest("multiple_caches_are_independent", &testMultipleCachesAreIndependent);
    abi.kernelTest("large_alignment_slot_computation", &testLargeAlignmentSlotComputation);
    abi.kernelTest("destroy_cache_actually_unmaps_descriptor", &testDestroyCacheActuallyUnmapsDescriptor);
    abi.kernelTest("alloc_free_interleaved_many_objects", &testAllocFreeInterleavedManyObjects);
}
