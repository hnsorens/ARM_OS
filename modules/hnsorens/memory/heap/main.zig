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

const vmm_if = abi.importInterface(abi.Vmm);
pub const mmu_if = abi.importInterface(abi.Mmu);
pub const serial_if = abi.importInterface(abi.Serial);

const HEAP_MIN_BLOCK_SIZE: u64 = 32;
const HEAP_MAGIC_ALLOCATED: u32 = 0x414C4F43; // "ALOC"
const HEAP_MAGIC_FREE: u32 = 0x46524545; // "FREE"

fn heapAlign(x: u64) u64 {
    return (x + 15) & ~@as(u64, 15);
}

pub const HeapBlock = extern struct {
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

pub const BLOCK_HEADER_SIZE: u64 = heapAlign(@sizeOf(HeapBlock));
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

pub fn heapCreate(root: u64, sz_in: u64, out_heap: *?*anyopaque) callconv(.c) c_int {
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

pub fn heapDestroy(heap_opaque: ?*anyopaque) callconv(.c) c_int {
    const heap = asCtx(heap_opaque) orelse return abi.EINVAL;
    if (heap.vaddr_base == 0) return abi.EINVAL;
    return vmm_if.free(heap.vmm_root, heap.vaddr_base, heap.total_size);
}

pub fn heapMalloc(heap_opaque: ?*anyopaque, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
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

pub fn heapFree(heap_opaque: ?*anyopaque, ptr: ?*anyopaque) callconv(.c) c_int {
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

pub fn heapRealloc(heap_opaque: ?*anyopaque, ptr: ?*anyopaque, new_size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
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

pub fn heapMemalign(heap_opaque: ?*anyopaque, alignment_in: u64, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int {
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

pub fn heapGetStats(heap_opaque: ?*anyopaque, used: *u64, total: *u64) callconv(.c) c_int {
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

comptime {
    _ = @import("test.zig");
}
