//! Binary-buddy physical page-frame allocator, exporting `Pmm` (category
//! "pmm"). Order-indexed free lists are threaded directly through free
//! physical pages themselves (via their HHDM alias), so freeing a page
//! costs no separate bookkeeping allocation. A parallel `PageMeta` array
//! (one entry per physical page) tracks order/free/refcount state and is
//! bootstrapped by carving space directly out of the first sufficiently
//! large free region of the boot memory map.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

const serial_if = abi.importInterfaceAny(abi.Serial);

const PAGE_SIZE: u64 = 4096;
const MAX_ORDER: u8 = 64;
/// Highest order `pmm_init`'s block-splitting scan may start at without
/// `(1 << order) * PAGE_SIZE` overflowing u64 (order 52 already wraps,
/// since PAGE_SIZE is 2^12) -- see the comment at its use site.
const MAX_SAFE_SPLIT_ORDER: u8 = 51;

const PageMeta = struct {
    ref_count: u32 = 0,
    order: u8 = 0,
    is_free: bool = false,
    flags: u16 = 0,
};

const BlockNode = struct {
    next: ?*BlockNode = null,
    prev: ?*BlockNode = null,
};

const OrderList = struct {
    head: ?*BlockNode = null,
    block_count: u64 = 0,
};

const Allocator = struct {
    orders: [MAX_ORDER]OrderList = [_]OrderList{.{}} ** MAX_ORDER,
    total_memory_bytes: u64 = 0,
    free_memory_bytes: u64 = 0,
};

var g_allocator: Allocator = .{};
var g_meta_array: []PageMeta = &.{};
var g_total_pages: u64 = 0;
var g_hhdm_offset: u64 = 0;

fn physToKv(phys: u64) *anyopaque {
    return @ptrFromInt(phys + g_hhdm_offset);
}

fn kvToPhys(virt: *anyopaque) u64 {
    return @intFromPtr(virt) - g_hhdm_offset;
}

fn shiftOf(order: u8) u6 {
    return @intCast(order);
}

fn listAdd(order: u8, node: *BlockNode) void {
    const list = &g_allocator.orders[order];
    node.next = list.head;
    node.prev = null;
    if (list.head) |h| h.prev = node;
    list.head = node;
    list.block_count += 1;
}

fn listRemove(order: u8, node: *BlockNode) void {
    const list = &g_allocator.orders[order];
    if (node.prev) |p| p.next = node.next else list.head = node.next;
    if (node.next) |n| n.prev = node.prev;
    list.block_count -= 1;
}

fn markPagesUsed(base_idx: u64, count: u64) void {
    var p: u64 = 0;
    while (p < count) : (p += 1) g_meta_array[base_idx + p].is_free = false;
}

/// Splits a block of `start_order` at `phys` down to `target_order`,
/// threading each freed buddy half back onto its own order's free list.
fn splitDownTo(start_order: u8, phys: u64, target_order: u8) void {
    var order = start_order;
    while (order > target_order) {
        order -= 1;
        const split_block_size = (@as(u64, 1) << shiftOf(order)) * PAGE_SIZE;
        const buddy_phys = phys + split_block_size;
        const buddy_index = buddy_phys / PAGE_SIZE;
        const buddy_pages = @as(u64, 1) << shiftOf(order);

        var p: u64 = 0;
        while (p < buddy_pages) : (p += 1) {
            g_meta_array[buddy_index + p] = .{ .is_free = true, .order = order, .ref_count = 0 };
        }

        const buddy_node: *BlockNode = @ptrCast(@alignCast(physToKv(buddy_phys)));
        listAdd(order, buddy_node);
    }
}

fn computeTargetOrder(count: u64) ?u8 {
    var order: u8 = 0;
    while ((@as(u64, 1) << shiftOf(order)) < count) {
        order += 1;
        if (order >= MAX_ORDER) return null;
    }
    return order;
}

// --- Initialization ------------------------------------------------------

pub fn init(memory_regions: []abi.MemoryRegion, hhdm_offset: u64) void {
    g_hhdm_offset = hhdm_offset;

    var highest_address: u64 = 0;
    for (memory_regions) |r| {
        const end = r.start + r.page_count * PAGE_SIZE;
        if (end > highest_address) highest_address = end;
    }

    g_total_pages = highest_address / PAGE_SIZE;
    const meta_array_size = g_total_pages * @sizeOf(PageMeta);
    const meta_pages_needed = (meta_array_size + PAGE_SIZE - 1) / PAGE_SIZE;

    var meta_phys_alloc_start: u64 = 0;
    for (memory_regions) |*r| {
        if (r.memory_type == .free and r.page_count >= meta_pages_needed) {
            meta_phys_alloc_start = r.start;
            r.start += meta_pages_needed * PAGE_SIZE;
            r.page_count -= meta_pages_needed;
            break;
        }
    }

    const meta_ptr: [*]PageMeta = @ptrCast(@alignCast(physToKv(meta_phys_alloc_start)));
    g_meta_array = meta_ptr[0..g_total_pages];
    @memset(g_meta_array, PageMeta{});

    g_allocator = .{};

    for (memory_regions) |r| {
        if (r.memory_type != .free) continue;

        var chunk_cursor = r.start;
        const chunk_end = chunk_cursor + r.page_count * PAGE_SIZE;

        while (chunk_cursor < chunk_end) {
            const remaining_bytes = chunk_end - chunk_cursor;
            // Scanning from MAX_ORDER-1 (63) downward would compute
            // `(1 << 63) * PAGE_SIZE` on the first iteration, overflowing
            // u64 (order 52 is already the first overflowing value, since
            // PAGE_SIZE is 2^12). No real region is anywhere near that
            // large, so start at the highest order that can't overflow.
            var target_order: u8 = MAX_SAFE_SPLIT_ORDER;
            while (target_order > 0) {
                const block_bytes = (@as(u64, 1) << shiftOf(target_order)) * PAGE_SIZE;
                if (block_bytes <= remaining_bytes and (chunk_cursor % block_bytes) == 0) break;
                target_order -= 1;
            }

            const allocated_bytes = (@as(u64, 1) << shiftOf(target_order)) * PAGE_SIZE;
            const base_page_idx = chunk_cursor / PAGE_SIZE;
            const block_pages = @as(u64, 1) << shiftOf(target_order);

            var p: u64 = 0;
            while (p < block_pages) : (p += 1) {
                g_meta_array[base_page_idx + p] = .{ .is_free = true, .order = target_order, .ref_count = 0 };
            }

            const node: *BlockNode = @ptrCast(@alignCast(physToKv(chunk_cursor)));
            listAdd(target_order, node);

            g_allocator.total_memory_bytes += allocated_bytes;
            g_allocator.free_memory_bytes += allocated_bytes;

            chunk_cursor += allocated_bytes;
        }
    }
}

// --- Allocation ------------------------------------------------------------

fn allocPage(page_order: u8, out_frame: *u64) callconv(.c) c_int {
    if (page_order >= MAX_ORDER) return abi.EINVAL;

    var current_order = page_order;
    while (current_order < MAX_ORDER) : (current_order += 1) {
        const chosen_node = g_allocator.orders[current_order].head orelse continue;
        const found_block_phys = kvToPhys(chosen_node);
        listRemove(current_order, chosen_node);

        markPagesUsed(found_block_phys / PAGE_SIZE, @as(u64, 1) << shiftOf(current_order));
        splitDownTo(current_order, found_block_phys, page_order);

        const idx = found_block_phys / PAGE_SIZE;
        g_meta_array[idx].order = page_order;
        g_meta_array[idx].ref_count = 1;
        g_allocator.free_memory_bytes -= (@as(u64, 1) << shiftOf(page_order)) * PAGE_SIZE;

        out_frame.* = found_block_phys;
        return 0;
    }
    return abi.ENOMEM;
}

fn freePage(page_order: u8, frame: u64) c_int {
    if (page_order >= MAX_ORDER or (frame % PAGE_SIZE) != 0) return abi.EINVAL;

    var current_order = page_order;
    var current_frame = frame;
    const initial_block_bytes = (@as(u64, 1) << shiftOf(page_order)) * PAGE_SIZE;

    while (current_order < MAX_ORDER - 1) {
        const block_bytes = (@as(u64, 1) << shiftOf(current_order)) * PAGE_SIZE;
        const buddy_frame = current_frame ^ block_bytes;
        const buddy_index = buddy_frame / PAGE_SIZE;

        if (buddy_index >= g_total_pages) break;
        if (!g_meta_array[buddy_index].is_free or g_meta_array[buddy_index].order != current_order) break;

        const buddy_node: *BlockNode = @ptrCast(@alignCast(physToKv(buddy_frame)));
        listRemove(current_order, buddy_node);

        markPagesUsed(buddy_index, @as(u64, 1) << shiftOf(current_order));

        current_frame = @min(current_frame, buddy_frame);
        current_order += 1;
    }

    const final_index = current_frame / PAGE_SIZE;
    const final_pages = @as(u64, 1) << shiftOf(current_order);
    var p: u64 = 0;
    while (p < final_pages) : (p += 1) {
        g_meta_array[final_index + p] = .{ .is_free = true, .order = current_order, .ref_count = 0 };
    }

    const final_node: *BlockNode = @ptrCast(@alignCast(physToKv(current_frame)));
    listAdd(current_order, final_node);

    g_allocator.free_memory_bytes += initial_block_bytes;
    return 0;
}

fn allocAligned(count: u64, alignment: u64, out: *u64) callconv(.c) c_int {
    if (count == 0 or alignment < PAGE_SIZE or (alignment & (alignment - 1)) != 0) return abi.EINVAL;
    const target_order = computeTargetOrder(count) orelse return abi.EINVAL;

    var o: u8 = target_order;
    while (o < MAX_ORDER) : (o += 1) {
        var curr = g_allocator.orders[o].head;
        while (curr) |node| : (curr = node.next) {
            const phys = kvToPhys(node);
            if (phys % alignment != 0) continue;

            listRemove(o, node);
            markPagesUsed(phys / PAGE_SIZE, @as(u64, 1) << shiftOf(o));
            splitDownTo(o, phys, target_order);

            const idx = phys / PAGE_SIZE;
            g_meta_array[idx].order = target_order;
            g_meta_array[idx].ref_count = 1;
            g_allocator.free_memory_bytes -= (@as(u64, 1) << shiftOf(target_order)) * PAGE_SIZE;

            out.* = phys;
            return 0;
        }
    }
    return abi.ENOMEM;
}

fn allocInRange(count: u64, max_addr: u64, out: *u64) callconv(.c) c_int {
    if (count == 0) return abi.EINVAL;
    const target_order = computeTargetOrder(count) orelse return abi.EINVAL;

    var o: u8 = target_order;
    while (o < MAX_ORDER) : (o += 1) {
        var curr = g_allocator.orders[o].head;
        while (curr) |node| : (curr = node.next) {
            const phys = kvToPhys(node);
            const allocation_bytes = (@as(u64, 1) << shiftOf(target_order)) * PAGE_SIZE;
            if (phys + allocation_bytes > max_addr) continue;

            listRemove(o, node);
            markPagesUsed(phys / PAGE_SIZE, @as(u64, 1) << shiftOf(o));
            splitDownTo(o, phys, target_order);

            const idx = phys / PAGE_SIZE;
            g_meta_array[idx].order = target_order;
            g_meta_array[idx].ref_count = 1;
            g_allocator.free_memory_bytes -= allocation_bytes;

            out.* = phys;
            return 0;
        }
    }
    return abi.ENOMEM;
}

// --- Reference management ---------------------------------------------------

fn retain(frame: u64) callconv(.c) c_int {
    const idx = frame / PAGE_SIZE;
    if (idx >= g_total_pages or g_meta_array[idx].is_free) return abi.EFAULT;
    g_meta_array[idx].ref_count += 1;
    return 0;
}

fn release(frame: u64) callconv(.c) c_int {
    const idx = frame / PAGE_SIZE;
    if (idx >= g_total_pages or g_meta_array[idx].is_free) return abi.EFAULT;

    if (g_meta_array[idx].ref_count > 0) {
        g_meta_array[idx].ref_count -= 1;
        if (g_meta_array[idx].ref_count == 0) {
            return freePage(g_meta_array[idx].order, frame);
        }
        return 0;
    }
    return abi.EFAULT;
}

// --- Diagnostics & reservation -----------------------------------------------

fn getTotalMemory() callconv(.c) u64 {
    return g_allocator.total_memory_bytes;
}

fn getFreeMemory() callconv(.c) u64 {
    return g_allocator.free_memory_bytes;
}

fn reserveRange(start: u64, sz: u64) callconv(.c) c_int {
    if (start % PAGE_SIZE != 0 or sz == 0) return 0;

    const start_idx = start / PAGE_SIZE;
    const pages_to_reserve = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    const end_idx = start_idx + pages_to_reserve;
    if (end_idx > g_total_pages) return abi.EINVAL;

    var idx = start_idx;
    while (idx < end_idx) : (idx += 1) {
        if (!g_meta_array[idx].is_free) continue;

        const order = g_meta_array[idx].order;
        const block_base_idx = idx & ~((@as(u64, 1) << shiftOf(order)) - 1);
        const block_base_phys = block_base_idx * PAGE_SIZE;
        const node: *BlockNode = @ptrCast(@alignCast(physToKv(block_base_phys)));

        listRemove(order, node);

        const block_pages = @as(u64, 1) << shiftOf(order);
        var p: u64 = 0;
        while (p < block_pages) : (p += 1) {
            g_meta_array[block_base_idx + p] = .{ .is_free = false, .ref_count = 1, .order = 0 };
        }

        g_allocator.free_memory_bytes -= (@as(u64, 1) << shiftOf(order)) * PAGE_SIZE;
        // Skip past the whole block just processed; the loop's own
        // increment then lands one past it.
        idx = block_base_idx + block_pages - 1;
    }
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    const boot_info: *abi.BootInfo = @ptrCast(@alignCast(boot_info_ptr));
    const regions = boot_info.memory_regions[0..boot_info.memory_map_size];
    init(regions, abi.HHDM_OFFSET);
}

comptime {
    abi.exportInterface("buddy", abi.Pmm, .{
        .alloc_page = allocPage,
        .alloc_aligned = allocAligned,
        .alloc_in_range = allocInRange,
        .retain = retain,
        .release = release,
        .get_total_memory = getTotalMemory,
        .get_free_memory = getFreeMemory,
        .reserve_range = reserveRange,
    });
}

// --- Unit tests --------------------------------------------------------------

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
                t.expectEqual(@src(), allocPage(order, &allocs[i]), 0);
            }
            for (0..NUM_PAGES) |i| {
                for (0..i) |j| {
                    t.expectNotEqual(@src(), allocs[i], allocs[j]);
                }
            }
            for (0..NUM_PAGES) |i| {
                t.expectEqual(@src(), release(allocs[i]), 0);
                t.expectNotEqual(@src(), release(allocs[i]), 0);
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
            t.expectEqual(@src(), allocPage(order, &allocs[i]), 0);
            for (0..RETAIN_COUNT) |_| {
                t.expectEqual(@src(), retain(allocs[i]), 0);
            }
            for (0..RETAIN_COUNT) |_| {
                t.expectEqual(@src(), release(allocs[i]), 0);
            }
            t.expectEqual(@src(), release(allocs[i]), 0);
            t.expectNotEqual(@src(), release(allocs[i]), 0);
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
        t.expectEqual(@src(), allocAligned(counts[i], alignments[i], &allocations[i]), 0);
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
        t.expectEqual(@src(), release(allocations[i]), 0);
    }
    return t.result();
}

fn testRangeZoneAlloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var dma_frame: u64 = 0;
    var dma32_frame: u64 = 0;

    if (allocInRange(4, 16 * 1024 * 1024, &dma_frame) == 0) {
        t.expectLessThan(@src(), dma_frame + 4 * 4096, 16 * 1024 * 1024);
    }

    if (allocInRange(32, 0x100000000, &dma32_frame) == 0) {
        t.expectLessThan(@src(), dma32_frame + 32 * 4096, 0x100000000);
    }

    var invalid_frame: u64 = 0;
    t.expectNotEqual(@src(), allocInRange(1, 0, &invalid_frame), 0);

    if (dma_frame != 0) t.expectEqual(@src(), release(dma_frame), 0);
    if (dma32_frame != 0) t.expectEqual(@src(), release(dma32_frame), 0);
    return t.result();
}

fn testReserveAndExhaustion() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    const free_before = getFreeMemory();

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
    t.expectEqual(@src(), allocPage(2, &scratch_frame), 0);
    t.expectEqual(@src(), release(scratch_frame), 0);

    const status = reserveRange(scratch_frame, reserve_size);

    if (status == 0) {
        const free_after = getFreeMemory();
        t.expectLessOrEqual(@src(), free_after, free_before - reserve_size);

        var test_alloc: u64 = undefined;
        t.expectEqual(@src(), allocPage(0, &test_alloc), 0);

        const within_reserved = test_alloc >= scratch_frame and test_alloc < scratch_frame + reserve_size;
        t.expectFalse(@src(), within_reserved);

        t.expectEqual(@src(), release(test_alloc), 0);
    }
    return t.result();
}

fn testStatsConsistency() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const ALLOC_COUNT = 64;

    const initial_free = getFreeMemory();
    var allocations: [ALLOC_COUNT]u64 = undefined;

    for (0..ALLOC_COUNT) |i| {
        t.expectEqual(@src(), allocPage(0, &allocations[i]), 0);
        t.expectLessThan(@src(), getFreeMemory(), initial_free);
    }
    for (0..ALLOC_COUNT) |i| {
        t.expectEqual(@src(), release(allocations[i]), 0);
    }
    t.expectEqual(@src(), getFreeMemory(), initial_free);
    return t.result();
}

fn testRobustnessEdgeCase() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectNotEqual(@src(), retain(0xFFFFFFFFFFFFF000), 0);
    t.expectNotEqual(@src(), release(0xFFFFFFFFFFFFF000), 0);

    var overflow_frame: u64 = undefined;
    t.expectNotEqual(@src(), allocPage(255, &overflow_frame), 0);

    var out_align: u64 = undefined;
    t.expectNotEqual(@src(), allocAligned(0, 4096, &out_align), 0);
    t.expectNotEqual(@src(), allocAligned(1, 4097, &out_align), 0);
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
}
