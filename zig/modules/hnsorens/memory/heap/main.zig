//! Intrusive boundary-tag first-fit heap allocator, exporting `Heap`
//! (category "heap"). Self-bootstraps: one `vmm.allocate` call gets a
//! region whose first bytes hold the `HeapContext` header itself,
//! immediately followed by one giant free `HeapBlock` spanning the rest.
//!
//! The C reference's exported vtable omits `.create`/`.destroy` entirely
//! (a real bug -- those fields were simply never populated). Zig's struct
//! literals require every field, so this port properly exports them.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

const vmm_if = abi.importInterfaceAny(abi.Vmm);
const mmu_if = abi.importInterfaceAny(abi.Mmu);
const serial_if = abi.importInterfaceAny(abi.Serial);

const HEAP_MIN_BLOCK_SIZE: u64 = 32;
const HEAP_MAGIC_ALLOCATED: u32 = 0x414C4F43; // "ALOC"
const HEAP_MAGIC_FREE: u32 = 0x46524545; // "FREE"

fn heapAlign(x: u64) u64 {
    return (x + 15) & ~@as(u64, 15);
}

const HeapBlock = extern struct {
    magic: u32 = 0,
    is_free: bool = false,
    size: u64 = 0,
    next: ?*HeapBlock = null,
    prev: ?*HeapBlock = null,
};

const HeapContext = extern struct {
    vmm_root: u64 = 0,
    vaddr_base: u64 = 0,
    total_size: u64 = 0,
    used_size: u64 = 0,
    head: ?*HeapBlock = null,
};

const BLOCK_HEADER_SIZE: u64 = heapAlign(@sizeOf(HeapBlock));
const CONTEXT_SIZE: u64 = heapAlign(@sizeOf(HeapContext));

fn asCtx(h: ?*anyopaque) ?*HeapContext {
    return @ptrCast(@alignCast(h));
}

fn splitBlock(block: *HeapBlock, size: u64) void {
    const rem_size = block.size - size;
    if (rem_size < BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE) return;

    const new_block: *HeapBlock = @ptrFromInt(@intFromPtr(block) + BLOCK_HEADER_SIZE + size);
    new_block.magic = HEAP_MAGIC_FREE;
    new_block.is_free = true;
    new_block.size = rem_size - BLOCK_HEADER_SIZE;
    new_block.next = block.next;
    new_block.prev = block;

    if (block.next) |n| n.prev = new_block;
    block.next = new_block;
    block.size = size;
}

fn coalesceBlocks(block: *HeapBlock) void {
    if (block.next) |next_block| {
        if (next_block.is_free) {
            block.size += BLOCK_HEADER_SIZE + next_block.size;
            block.next = next_block.next;
            if (next_block.next) |n| n.prev = block;
            next_block.magic = 0;
        }
    }

    if (block.prev) |prev_block| {
        if (prev_block.is_free) {
            prev_block.size += BLOCK_HEADER_SIZE + block.size;
            prev_block.next = block.next;
            if (block.next) |n| n.prev = prev_block;
            block.magic = 0;
        }
    }
}

fn heapCreate(root: u64, sz_in: u64, out_heap: *?*anyopaque) callconv(.c) c_int {
    if (root == 0) return abi.EINVAL;

    var vaddr: u64 = 0xFFFF900000000000; // Standardized kernel canonical heap base.
    var sz = sz_in + CONTEXT_SIZE + BLOCK_HEADER_SIZE;
    sz = (sz + 4095) & ~@as(u64, 4095);

    const status = vmm_if.allocate(root, &vaddr, sz, 0x713, .heap);
    if (status != 0) return status;

    const heap_slot: *HeapContext = @ptrFromInt(vaddr);
    heap_slot.vmm_root = root;
    heap_slot.vaddr_base = vaddr;
    heap_slot.total_size = sz;
    heap_slot.used_size = 0;

    const root_block: *HeapBlock = @ptrFromInt(vaddr + CONTEXT_SIZE);
    root_block.magic = HEAP_MAGIC_FREE;
    root_block.is_free = true;
    root_block.size = sz - CONTEXT_SIZE - BLOCK_HEADER_SIZE;
    root_block.next = null;
    root_block.prev = null;

    heap_slot.head = root_block;
    out_heap.* = @ptrCast(heap_slot);
    return 0;
}

fn heapDestroy(heap_opaque: ?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;
    if (heap.vaddr_base == 0) return abi.EINVAL;
    return vmm_if.free(heap.vmm_root, heap.vaddr_base, heap.total_size);
}

fn heapMalloc(heap_opaque: ?*anyopaque, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;

    if (size == 0) {
        out_ptr.* = null;
        return 0;
    }

    const aligned_size = heapAlign(size);
    var curr = heap.head;
    while (curr) |c| : (curr = c.next) {
        if (c.is_free and c.size >= aligned_size) {
            splitBlock(c, aligned_size);
            c.is_free = false;
            c.magic = HEAP_MAGIC_ALLOCATED;
            heap.used_size += BLOCK_HEADER_SIZE + c.size;
            out_ptr.* = @ptrFromInt(@intFromPtr(c) + BLOCK_HEADER_SIZE);
            return 0;
        }
    }

    out_ptr.* = null;
    return abi.ENOMEM;
}

fn heapFree(heap_opaque: ?*anyopaque, ptr: ?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;
    const p = ptr orelse return 0; // free(NULL) is a no-op.

    const block: *HeapBlock = @ptrFromInt(@intFromPtr(p) - BLOCK_HEADER_SIZE);
    if (block.magic != HEAP_MAGIC_ALLOCATED) return abi.EINVAL;

    block.is_free = true;
    block.magic = HEAP_MAGIC_FREE;

    if (heap.used_size >= BLOCK_HEADER_SIZE + block.size) {
        heap.used_size -= BLOCK_HEADER_SIZE + block.size;
    } else {
        heap.used_size = 0;
    }

    coalesceBlocks(block);
    return 0;
}

fn heapRealloc(heap_opaque: ?*anyopaque, ptr: ?*anyopaque, new_size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;
    const p = ptr orelse return heapMalloc(heap_opaque, new_size, out_ptr);

    if (new_size == 0) {
        _ = heapFree(heap_opaque, p);
        out_ptr.* = null;
        return 0;
    }

    const block: *HeapBlock = @ptrFromInt(@intFromPtr(p) - BLOCK_HEADER_SIZE);
    const aligned_size = heapAlign(new_size);
    if (block.magic != HEAP_MAGIC_ALLOCATED) return abi.EINVAL;

    // Path Alpha: already big enough -- shrink/reuse in place.
    if (block.size >= aligned_size) {
        splitBlock(block, aligned_size);
        out_ptr.* = p;
        return 0;
    }

    // Path Beta: consume a free forward neighbor to avoid migrating.
    if (block.next) |next_block| {
        if (next_block.is_free and (block.size + BLOCK_HEADER_SIZE + next_block.size) >= aligned_size) {
            heap.used_size -= BLOCK_HEADER_SIZE + block.size;

            block.size += BLOCK_HEADER_SIZE + next_block.size;
            block.next = next_block.next;
            if (next_block.next) |n| n.prev = block;
            next_block.magic = 0;

            splitBlock(block, aligned_size);
            heap.used_size += BLOCK_HEADER_SIZE + block.size;
            out_ptr.* = p;
            return 0;
        }
    }

    // Path Gamma: migrate.
    var new_ptr: ?*anyopaque = null;
    const status = heapMalloc(heap_opaque, new_size, &new_ptr);
    if (status != 0) return status;

    const dst: [*]u8 = @ptrCast(new_ptr.?);
    const src: [*]const u8 = @ptrCast(p);
    @memcpy(dst[0..block.size], src[0..block.size]);
    _ = heapFree(heap_opaque, p);
    out_ptr.* = new_ptr;
    return 0;
}

fn heapMemalign(heap_opaque: ?*anyopaque, alignment_in: u64, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;

    if (alignment_in == 0 or (alignment_in & (alignment_in - 1)) != 0) return abi.EINVAL;

    const alignment = @max(alignment_in, 16);

    if (size == 0) {
        out_ptr.* = null;
        return 0;
    }

    const aligned_size = heapAlign(size);
    var curr = heap.head;
    while (curr) |c| : (curr = c.next) {
        if (!c.is_free) continue;

        const raw_payload_addr = @intFromPtr(c) + BLOCK_HEADER_SIZE;
        var aligned_payload_addr = (raw_payload_addr + alignment - 1) & ~(alignment - 1);
        var padding = aligned_payload_addr - raw_payload_addr;

        // Case 1: this block's natural start already satisfies the alignment.
        if (padding == 0 and c.size >= aligned_size) {
            splitBlock(c, aligned_size);
            c.is_free = false;
            c.magic = HEAP_MAGIC_ALLOCATED;
            heap.used_size += BLOCK_HEADER_SIZE + c.size;
            out_ptr.* = @ptrFromInt(aligned_payload_addr);
            return 0;
        }

        // If the leading pad is too small to host its own header, push the
        // alignment target forward by whole alignment chunks until it is.
        const min_required_padding = BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE;
        if (padding > 0 and padding < min_required_padding) {
            const remaining_pad = min_required_padding - padding;
            const alignment_chunks = (remaining_pad + alignment - 1) & ~(alignment - 1);
            padding += alignment_chunks;
            aligned_payload_addr = raw_payload_addr + padding;
        }

        // Case 2: carve off the leading pad as its own free block, then
        // allocate the aligned remainder.
        if (padding >= min_required_padding and c.size >= (padding + aligned_size)) {
            const old_total_size = c.size;
            c.size = padding - BLOCK_HEADER_SIZE;

            const aligned_block: *HeapBlock = @ptrFromInt(aligned_payload_addr - BLOCK_HEADER_SIZE);
            aligned_block.magic = HEAP_MAGIC_FREE;
            aligned_block.is_free = true;
            aligned_block.size = old_total_size - padding;

            aligned_block.next = c.next;
            aligned_block.prev = c;
            if (c.next) |n| n.prev = aligned_block;
            c.next = aligned_block;

            splitBlock(aligned_block, aligned_size);
            aligned_block.is_free = false;
            aligned_block.magic = HEAP_MAGIC_ALLOCATED;

            heap.used_size += BLOCK_HEADER_SIZE + aligned_block.size;
            out_ptr.* = @ptrFromInt(aligned_payload_addr);
            return 0;
        }
    }

    out_ptr.* = null;
    return abi.ENOMEM;
}

fn heapGetStats(heap_opaque: ?*anyopaque, used: *u64, total: *u64) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;
    used.* = heap.used_size;
    total.* = heap.total_size;
    return 0;
}

comptime {
    abi.exportInterface("heap", abi.Heap, .{
        .create = heapCreate,
        .destroy = heapDestroy,
        .malloc = heapMalloc,
        .free = heapFree,
        .realloc = heapRealloc,
        .memalign = heapMemalign,
        .get_stats = heapGetStats,
    });
}

// --- Unit tests --------------------------------------------------------------

fn testLifecycleAndBasicMalloc() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&vmm_root);
    const initial_size: u64 = 64 * 1024;

    var heap_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), heapCreate(vmm_root, initial_size, &heap_opaque), 0);
    t.expectTrue(@src(), heap_opaque != null);

    var used: u64 = 0;
    var total: u64 = 0;
    t.expectEqual(@src(), heapGetStats(heap_opaque, &used, &total), 0);
    t.expectEqual(@src(), used, 0);
    t.expectLessThan(@src(), initial_size, total);

    var ptr1: ?*anyopaque = null;
    t.expectEqual(@src(), heapMalloc(heap_opaque, 256, &ptr1), 0);
    t.expectTrue(@src(), ptr1 != null);

    _ = heapGetStats(heap_opaque, &used, &total);
    t.expectLessOrEqual(@src(), @as(u64, 256), used);

    t.expectEqual(@src(), heapFree(heap_opaque, ptr1), 0);

    _ = heapGetStats(heap_opaque, &used, &total);
    t.expectEqual(@src(), used, 0);

    t.expectEqual(@src(), heapDestroy(heap_opaque), 0);
    return t.result();
}

fn testFragmentationAndCoalescing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    t.expectEqual(@src(), heapCreate(vmm_root, 128 * 1024, &heap_opaque), 0);

    var p1: ?*anyopaque = null;
    var p2: ?*anyopaque = null;
    var p3: ?*anyopaque = null;
    t.expectEqual(@src(), heapMalloc(heap_opaque, 1024, &p1), 0);
    t.expectEqual(@src(), heapMalloc(heap_opaque, 1024, &p2), 0);
    t.expectEqual(@src(), heapMalloc(heap_opaque, 1024, &p3), 0);

    t.expectEqual(@src(), heapFree(heap_opaque, p1), 0);
    t.expectEqual(@src(), heapFree(heap_opaque, p3), 0);
    t.expectEqual(@src(), heapFree(heap_opaque, p2), 0);

    var final_used: u64 = 1;
    var total_dummy: u64 = undefined;
    _ = heapGetStats(heap_opaque, &final_used, &total_dummy);
    t.expectEqual(@src(), final_used, 0);

    _ = heapDestroy(heap_opaque);
    return t.result();
}

fn testReallocInPlaceAndMigration() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = heapCreate(vmm_root, 128 * 1024, &heap_opaque);

    var p1: ?*anyopaque = null;
    t.expectEqual(@src(), heapMalloc(heap_opaque, 128, &p1), 0);

    const data1: [*]u8 = @ptrCast(p1.?);
    for (0..128) |i| data1[i] = @truncate(i);

    var p2: ?*anyopaque = null;
    t.expectEqual(@src(), heapRealloc(heap_opaque, p1, 64, &p2), 0);
    t.expectEqual(@src(), @intFromPtr(p1), @intFromPtr(p2));

    const data2: [*]const u8 = @ptrCast(p2.?);
    for (0..64) |i| t.expectEqual(@src(), data2[i], @as(u8, @truncate(i)));

    var barrier: ?*anyopaque = null;
    _ = heapMalloc(heap_opaque, 256, &barrier);

    var p3: ?*anyopaque = null;
    t.expectEqual(@src(), heapRealloc(heap_opaque, p2, 2048, &p3), 0);
    t.expectNotEqual(@src(), @intFromPtr(p2), @intFromPtr(p3));

    const data3: [*]const u8 = @ptrCast(p3.?);
    for (0..64) |i| t.expectEqual(@src(), data3[i], @as(u8, @truncate(i)));

    _ = heapFree(heap_opaque, barrier);
    _ = heapFree(heap_opaque, p3);
    _ = heapDestroy(heap_opaque);
    return t.result();
}

fn testMemalignBoundaryChecks() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = heapCreate(vmm_root, 512 * 1024, &heap_opaque);

    var aligned_ptr: ?*anyopaque = null;
    t.expectEqual(@src(), heapMemalign(heap_opaque, 64, 1023, &aligned_ptr), 0);
    t.expectTrue(@src(), aligned_ptr != null);
    t.expectEqual(@src(), @intFromPtr(aligned_ptr.?) & (64 - 1), 0);

    var page_aligned_ptr: ?*anyopaque = null;
    t.expectEqual(@src(), heapMemalign(heap_opaque, 4096, 8000, &page_aligned_ptr), 0);
    t.expectTrue(@src(), page_aligned_ptr != null);
    t.expectEqual(@src(), @intFromPtr(page_aligned_ptr.?) & (4096 - 1), 0);

    t.expectEqual(@src(), heapFree(heap_opaque, aligned_ptr), 0);
    t.expectEqual(@src(), heapFree(heap_opaque, page_aligned_ptr), 0);

    _ = heapDestroy(heap_opaque);
    return t.result();
}

fn testSecurityAndEdgeCases() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var vmm_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&vmm_root);

    var heap_opaque: ?*anyopaque = null;
    _ = heapCreate(vmm_root, 64 * 1024, &heap_opaque);

    var out_ptr: ?*anyopaque = @ptrFromInt(0xDEADBEEF);
    t.expectEqual(@src(), heapMalloc(heap_opaque, 0, &out_ptr), 0);
    t.expectTrue(@src(), out_ptr == null);

    t.expectEqual(@src(), heapFree(heap_opaque, null), 0);

    t.expectEqual(@src(), heapMemalign(heap_opaque, 31, 128, &out_ptr), abi.EINVAL);

    t.expectEqual(@src(), heapMalloc(heap_opaque, 1024 * 1024, &out_ptr), abi.ENOMEM);

    var corrupted_ptr: ?*anyopaque = null;
    _ = heapMalloc(heap_opaque, 64, &corrupted_ptr);

    const block: *HeapBlock = @ptrFromInt(@intFromPtr(corrupted_ptr.?) - BLOCK_HEADER_SIZE);
    const original_magic = block.magic;
    block.magic = 0xDEADC0DE;

    t.expectEqual(@src(), heapFree(heap_opaque, corrupted_ptr), abi.EINVAL);

    block.magic = original_magic;
    _ = heapFree(heap_opaque, corrupted_ptr);
    _ = heapDestroy(heap_opaque);
    return t.result();
}

comptime {
    abi.kernelTest("lifecycle_and_basic_malloc", &testLifecycleAndBasicMalloc);
    abi.kernelTest("fragmentation_and_coalescing", &testFragmentationAndCoalescing);
    abi.kernelTest("realloc_in_place_and_migration", &testReallocInPlaceAndMigration);
    abi.kernelTest("memalign_boundary_checks", &testMemalignBoundaryChecks);
    abi.kernelTest("security_and_edge_cases", &testSecurityAndEdgeCases);
}
