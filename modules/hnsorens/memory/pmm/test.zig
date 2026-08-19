//! Tests for the buddy physical page allocator in `main.zig`, split into
//! their own file (reachable from the build via `main.zig`'s `comptime {
//! _ = @import("test.zig"); }`).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testAlloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const NUM_PAGES = 128;
    const RUN_COUNT = 5;
    const ORDER_COUNT = 5;

    var allocs: [NUM_PAGES]u64 = undefined;
    var order: u8 = 0;
    while (order < ORDER_COUNT) : (order += 1) {
        var run: u32 = 0;
        while (run < RUN_COUNT) : (run += 1) {
            for (0..NUM_PAGES) |i| {
                t.expectEqual(@src(), main.allocPage(order, &allocs[i]), 0);
            }
            for (0..NUM_PAGES) |i| {
                for (0..i) |j| {
                    t.expectNotEqual(@src(), allocs[i], allocs[j]);
                }
            }
            for (0..NUM_PAGES) |i| {
                t.expectEqual(@src(), main.release(allocs[i]), 0);
                t.expectNotEqual(@src(), main.release(allocs[i]), 0);
            }
        }
    }
    return t.result();
}

fn testRetain() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const ALLOC_COUNT = 128;
    const ORDER_COUNT = 5;
    const RETAIN_COUNT = 100;

    var allocs: [ALLOC_COUNT]u64 = undefined;
    var order: u8 = 0;
    while (order < ORDER_COUNT) : (order += 1) {
        for (0..ALLOC_COUNT) |i| {
            t.expectEqual(@src(), main.allocPage(order, &allocs[i]), 0);
            for (0..RETAIN_COUNT) |_| {
                t.expectEqual(@src(), main.retain(allocs[i]), 0);
            }
            for (0..RETAIN_COUNT) |_| {
                t.expectEqual(@src(), main.release(allocs[i]), 0);
            }
            t.expectEqual(@src(), main.release(allocs[i]), 0);
            t.expectNotEqual(@src(), main.release(allocs[i]), 0);
        }
    }
    return t.result();
}

fn testAlignedAlloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const alignments = [_]u64{ 4096, 8192, 16384, 32768, 65536, 1024 * 1024, 2 * 1024 * 1024, 4096, 4096, 4096 };
    const counts = [_]u64{ 1, 2, 4, 1, 8, 16, 512, 1, 1, 1 };
    var allocations: [alignments.len]u64 = undefined;

    for (0..alignments.len) |i| {
        t.expectEqual(@src(), main.allocAligned(counts[i], alignments[i], &allocations[i]), 0);
        t.expectEqual(@src(), allocations[i] % alignments[i], 0);
    }

    for (0..alignments.len) |i| {
        const start_a = allocations[i];
        const end_a = start_a + counts[i] * 4096;
        for (i + 1..alignments.len) |j| {
            const start_b = allocations[j];
            const end_b = start_b + counts[j] * 4096;
            const overlap = start_a < end_b and start_b < end_a;
            t.expectFalse(@src(), overlap);
        }
    }

    for (0..alignments.len) |i| {
        t.expectEqual(@src(), main.release(allocations[i]), 0);
    }
    return t.result();
}

fn testRangeZoneAlloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var dma_frame: u64 = 0;
    var dma32_frame: u64 = 0;

    if (main.allocInRange(4, 16 * 1024 * 1024, &dma_frame) == 0) {
        t.expectLessThan(@src(), dma_frame + 4 * 4096, 16 * 1024 * 1024);
    }

    if (main.allocInRange(32, 0x100000000, &dma32_frame) == 0) {
        t.expectLessThan(@src(), dma32_frame + 32 * 4096, 0x100000000);
    }

    var invalid_frame: u64 = 0;
    t.expectNotEqual(@src(), main.allocInRange(1, 0, &invalid_frame), 0);

    if (dma_frame != 0) t.expectEqual(@src(), main.release(dma_frame), 0);
    if (dma32_frame != 0) t.expectEqual(@src(), main.release(dma32_frame), 0);
    return t.result();
}

fn testReserveAndExhaustion() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    const free_before = main.getFreeMemory();

    // Allocate (and release) exactly a 4-page block, order 2, rather than
    // a single page: reserving a 4-page range starting from a released
    // single page would silently assume its 3 neighboring pages were also
    // already free, which they aren't guaranteed to be (e.g. a page used
    // by some earlier, unrelated allocation could legitimately sit right
    // next to it) -- allocating exactly the range we're about to reserve
    // guarantees it's genuinely free start to end, regardless of what
    // else has happened in the allocator by this point.
    const reserve_size: u64 = 4 * 4096;
    var scratch_frame: u64 = undefined;
    t.expectEqual(@src(), main.allocPage(2, &scratch_frame), 0);
    t.expectEqual(@src(), main.release(scratch_frame), 0);

    const status = main.reserveRange(scratch_frame, reserve_size);

    if (status == 0) {
        const free_after = main.getFreeMemory();
        t.expectLessOrEqual(@src(), free_after, free_before - reserve_size);

        var test_alloc: u64 = undefined;
        t.expectEqual(@src(), main.allocPage(0, &test_alloc), 0);

        const within_reserved = test_alloc >= scratch_frame and test_alloc < scratch_frame + reserve_size;
        t.expectFalse(@src(), within_reserved);

        t.expectEqual(@src(), main.release(test_alloc), 0);
    }
    return t.result();
}

fn testStatsConsistency() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const ALLOC_COUNT = 64;

    const initial_free = main.getFreeMemory();
    var allocations: [ALLOC_COUNT]u64 = undefined;

    for (0..ALLOC_COUNT) |i| {
        t.expectEqual(@src(), main.allocPage(0, &allocations[i]), 0);
        t.expectLessThan(@src(), main.getFreeMemory(), initial_free);
    }
    for (0..ALLOC_COUNT) |i| {
        t.expectEqual(@src(), main.release(allocations[i]), 0);
    }
    t.expectEqual(@src(), main.getFreeMemory(), initial_free);
    return t.result();
}

fn testRobustnessEdgeCase() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectNotEqual(@src(), main.retain(0xFFFFFFFFFFFFF000), 0);
    t.expectNotEqual(@src(), main.release(0xFFFFFFFFFFFFF000), 0);

    var overflow_frame: u64 = undefined;
    t.expectNotEqual(@src(), main.allocPage(255, &overflow_frame), 0);

    var out_align: u64 = undefined;
    t.expectNotEqual(@src(), main.allocAligned(0, 4096, &out_align), 0);
    t.expectNotEqual(@src(), main.allocAligned(1, 4097, &out_align), 0);
    return t.result();
}

fn testTotalMemoryStableAcrossChurn() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const total_before = main.getTotalMemory();

    var allocs: [32]u64 = undefined;
    for (0..32) |i| {
        t.expectEqual(@src(), main.allocPage(@intCast(i % 4), &allocs[i]), 0);
        // Total physical memory installed doesn't change just because
        // pages moved from free to used -- only get_free_memory should.
        t.expectEqual(@src(), main.getTotalMemory(), total_before);
    }
    for (0..32) |i| {
        t.expectEqual(@src(), main.release(allocs[i]), 0);
    }
    t.expectEqual(@src(), main.getTotalMemory(), total_before);
    return t.result();
}

fn testBuddyCoalescingRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const free_before = main.getFreeMemory();

    // A single order-0 allocation forces a split of some larger free
    // block; releasing it must walk all the way back up through buddy
    // coalescing and restore exactly the pre-allocation free byte count,
    // not leave permanently fragmented smaller blocks behind.
    var frame: u64 = undefined;
    t.expectEqual(@src(), main.allocPage(0, &frame), 0);
    t.expectEqual(@src(), main.release(frame), 0);
    t.expectEqual(@src(), main.getFreeMemory(), free_before);

    // Repeating the same request should land on the exact same address if
    // coalescing genuinely reconstituted the original block (deterministic
    // first-fit at a stable free-list state).
    var frame2: u64 = undefined;
    t.expectEqual(@src(), main.allocPage(0, &frame2), 0);
    t.expectEqual(@src(), frame2, frame);
    t.expectEqual(@src(), main.release(frame2), 0);
    return t.result();
}

fn testFreedPageRetainReleaseRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var frame: u64 = undefined;
    t.expectEqual(@src(), main.allocPage(0, &frame), 0);
    t.expectEqual(@src(), main.release(frame), 0);

    // `frame` is now back on a free list (is_free=true) -- retain/release
    // against it must be rejected the same way an out-of-range address
    // would be, not silently treat a free page as if it were live.
    t.expectNotEqual(@src(), main.retain(frame), 0);
    t.expectNotEqual(@src(), main.release(frame), 0);
    return t.result();
}

fn testMixedOrderStressChurn() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const initial_free = main.getFreeMemory();
    const ITERATIONS = 40;

    var allocs: [ITERATIONS]u64 = undefined;
    var orders: [ITERATIONS]u8 = undefined;

    for (0..ITERATIONS) |i| {
        const order: u8 = @intCast(i % 6);
        t.expectEqual(@src(), main.allocPage(order, &allocs[i]), 0);
        orders[i] = order;
    }

    // Release in reverse order, deliberately not mirroring allocation
    // order, to exercise buddy coalescing against arbitrarily-interleaved
    // neighbors rather than a tidy LIFO pattern.
    var i: usize = ITERATIONS;
    while (i > 0) {
        i -= 1;
        t.expectEqual(@src(), main.release(allocs[i]), 0);
    }

    t.expectEqual(@src(), main.getFreeMemory(), initial_free);
    return t.result();
}

fn testConcurrentOrdersDoNotOverlap() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const ORDER_COUNT = 6;

    var allocs: [ORDER_COUNT]u64 = undefined;
    for (0..ORDER_COUNT) |i| {
        t.expectEqual(@src(), main.allocPage(@intCast(i), &allocs[i]), 0);
    }

    for (0..ORDER_COUNT) |i| {
        const size_a = (@as(u64, 1) << @intCast(i)) * 4096;
        for (i + 1..ORDER_COUNT) |j| {
            const size_b = (@as(u64, 1) << @intCast(j)) * 4096;
            const overlap = allocs[i] < allocs[j] + size_b and allocs[j] < allocs[i] + size_a;
            t.expectFalse(@src(), overlap);
        }
    }

    for (0..ORDER_COUNT) |i| {
        t.expectEqual(@src(), main.release(allocs[i]), 0);
    }
    return t.result();
}

comptime {
    abi.kernelTest("alloc", &testAlloc);
    abi.kernelTest("retain", &testRetain);
    abi.kernelTest("aligned_alloc", &testAlignedAlloc);
    abi.kernelTest("range_zone_alloc", &testRangeZoneAlloc);
    abi.kernelTest("reserve_and_exhaustion", &testReserveAndExhaustion);
    abi.kernelTest("stats_consistency", &testStatsConsistency);
    abi.kernelTest("robustness_edge_case", &testRobustnessEdgeCase);
    abi.kernelTest("total_memory_stable_across_churn", &testTotalMemoryStableAcrossChurn);
    abi.kernelTest("buddy_coalescing_round_trip", &testBuddyCoalescingRoundTrip);
    abi.kernelTest("freed_page_retain_release_rejected", &testFreedPageRetainReleaseRejected);
    abi.kernelTest("mixed_order_stress_churn", &testMixedOrderStressChurn);
    abi.kernelTest("concurrent_orders_do_not_overlap", &testConcurrentOrdersDoNotOverlap);
}
