//! Fixed-size object slab allocator, exporting `Slab` (category "slab").
//! Each backing page carries a `KSlab` header at byte 0; free cells embed
//! their own "next free index" directly in their first 4 bytes (no
//! external bookkeeping table needed), giving O(1) alloc/free. Caches
//! keep three intrusive page queues (full/partial/empty).
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

pub const vmm_if = abi.importInterface(abi.Vmm);
pub const mmu_if = abi.importInterface(abi.Mmu);
pub const serial_if = abi.importInterface(abi.Serial);

pub const SLAB_PAGE_SIZE: u64 = 4096;
const SLAB_LIST_END: u32 = 0xFFFFFFFF;

const KSlab = extern struct {
    free_count: u32 = 0,
    next_free_slot: u32 = 0,
    next: ?*KSlab = null,
    prev: ?*KSlab = null,
};

pub const KSlabCache = extern struct {
    root: u64 = 0,
    obj_size: u64 = 0,
    alignment: u64 = 0,
    slots_per_slab: u64 = 0,
    slabs_full: ?*KSlab = null,
    slabs_partial: ?*KSlab = null,
    slabs_empty: ?*KSlab = null,
    is_allocated: bool = false,
};

pub fn asCache(h: ?*anyopaque) ?*KSlabCache {
    return @ptrCast(@alignCast(h));
}

fn getPayloadStart(slab: *KSlab, alignment: u64) u64 {
    const header_end = @intFromPtr(slab) + @sizeOf(KSlab);
    return (header_end + (alignment - 1)) & ~(alignment - 1);
}

fn enqueueSlab(head: *?*KSlab, slab: *KSlab) void {
    slab.next = head.*;
    slab.prev = null;
    if (head.*) |h| h.prev = slab;
    head.* = slab;
}

fn dequeueSlab(head: *?*KSlab, slab: *KSlab) void {
    if (slab.prev) |p| {
        p.next = slab.next;
    } else if (head.* == slab) {
        head.* = slab.next;
    }
    if (slab.next) |n| n.prev = slab.prev;
    slab.next = null;
    slab.prev = null;
}

pub fn createCache(root: u64, obj_size: u64, alignment: u64, out_cache: *?*anyopaque) callconv(.c) c_int {
    if (obj_size == 0 or alignment == 0) return abi.EINVAL;
    if ((alignment & (alignment - 1)) != 0) return abi.EINVAL;

    var aligned_obj_size = (obj_size + (alignment - 1)) & ~(alignment - 1);
    // Enforce a 4-byte minimum: a free cell embeds its own free-list link
    // (a u32) directly in its first bytes.
    if (aligned_obj_size < @sizeOf(u32)) aligned_obj_size = @sizeOf(u32);

    // Dry-run the payload offset for a slab header living at address 0, to
    // check the page can hold at least one object before allocating one.
    const header_end: u64 = @sizeOf(KSlab);
    const payload_start_offset = (header_end + (alignment - 1)) & ~(alignment - 1);
    if (aligned_obj_size > (SLAB_PAGE_SIZE - payload_start_offset)) return abi.EINVAL;

    var cache_vaddr: u64 = 0xFFFF900000000000;
    const status = vmm_if.allocate(root, &cache_vaddr, SLAB_PAGE_SIZE, 0x713, .heap);
    if (status != 0) return abi.ENOMEM;

    const cache: *KSlabCache = @ptrFromInt(cache_vaddr);
    cache.* = .{};
    cache.root = root;
    cache.obj_size = aligned_obj_size;
    cache.alignment = alignment;
    cache.slots_per_slab = (SLAB_PAGE_SIZE - payload_start_offset) / aligned_obj_size;

    if (cache.slots_per_slab == 0) {
        _ = vmm_if.free(root, cache_vaddr, SLAB_PAGE_SIZE);
        return abi.EINVAL;
    }

    cache.is_allocated = true;
    out_cache.* = @ptrCast(cache);
    return 0;
}

pub fn alloc(cache_opaque: ?*anyopaque, out_obj: *?*anyopaque) callconv(.c) c_int {
    const cache = asCache(cache_opaque) orelse return abi.EINVAL;
    if (!cache.is_allocated) return abi.EINVAL;

    var slab: *KSlab = undefined;
    var fresh_or_empty = false;

    // Prioritize partial pages to minimize fragmentation and page reuse.
    if (cache.slabs_partial) |p| {
        slab = p;
    } else if (cache.slabs_empty) |e| {
        slab = e;
        dequeueSlab(&cache.slabs_empty, slab);
        fresh_or_empty = true;
    } else {
        var data_vaddr: u64 = 0xFFFF900000000000;
        const status = vmm_if.allocate(cache.root, &data_vaddr, SLAB_PAGE_SIZE, 0x713, .heap);
        if (status != 0) return abi.ENOMEM;

        slab = @ptrFromInt(data_vaddr);
        slab.free_count = @intCast(cache.slots_per_slab);
        slab.next_free_slot = 0;
        slab.next = null;
        slab.prev = null;

        // Thread the free-list directly through the unused cells: each
        // holds the index of the next free cell, last one terminates.
        const payload_base = getPayloadStart(slab, cache.alignment);
        var i: u32 = 0;
        while (i < cache.slots_per_slab - 1) : (i += 1) {
            const next_link: *u32 = @ptrFromInt(payload_base + @as(u64, i) * cache.obj_size);
            next_link.* = i + 1;
        }
        const last_link: *u32 = @ptrFromInt(payload_base + (cache.slots_per_slab - 1) * cache.obj_size);
        last_link.* = SLAB_LIST_END;

        fresh_or_empty = true;
    }

    const payload_start = getPayloadStart(slab, cache.alignment);
    const alloc_addr = payload_start + @as(u64, slab.next_free_slot) * cache.obj_size;

    const link_ptr: *u32 = @ptrFromInt(alloc_addr);
    slab.next_free_slot = link_ptr.*;
    slab.free_count -= 1;

    out_obj.* = @ptrFromInt(alloc_addr);

    if (fresh_or_empty) {
        if (slab.free_count == 0) {
            enqueueSlab(&cache.slabs_full, slab);
        } else {
            enqueueSlab(&cache.slabs_partial, slab);
        }
    } else if (slab.free_count == 0) {
        dequeueSlab(&cache.slabs_partial, slab);
        enqueueSlab(&cache.slabs_full, slab);
    }

    return 0;
}

pub fn free(cache_opaque: ?*anyopaque, obj_opaque: ?*anyopaque) callconv(.c) c_int {
    const cache = asCache(cache_opaque) orelse return abi.EINVAL;
    const obj = obj_opaque orelse return abi.EINVAL;

    const obj_addr = @intFromPtr(obj);
    const slab: *KSlab = @ptrFromInt(obj_addr & ~(SLAB_PAGE_SIZE - 1));
    const payload_start = getPayloadStart(slab, cache.alignment);

    if (obj_addr < payload_start or obj_addr >= @intFromPtr(slab) + SLAB_PAGE_SIZE) return abi.EINVAL;

    const slot_idx: u32 = @intCast((obj_addr - payload_start) / cache.obj_size);
    if (slot_idx >= cache.slots_per_slab) return abi.EINVAL;

    if (slab.free_count == 0) {
        dequeueSlab(&cache.slabs_full, slab);
    } else if (slab.free_count + 1 == cache.slots_per_slab) {
        dequeueSlab(&cache.slabs_partial, slab);
    }

    const link_ptr: *u32 = @ptrFromInt(obj_addr);
    link_ptr.* = slab.next_free_slot;
    slab.next_free_slot = slot_idx;
    slab.free_count += 1;

    if (slab.free_count == cache.slots_per_slab) {
        enqueueSlab(&cache.slabs_empty, slab);
    } else if (slab.free_count - 1 == 0) {
        enqueueSlab(&cache.slabs_partial, slab);
    }

    return 0;
}

pub fn shrink(cache_opaque: ?*anyopaque) callconv(.c) c_int {
    const cache = asCache(cache_opaque) orelse return abi.EINVAL;
    if (!cache.is_allocated) return abi.EINVAL;

    var curr = cache.slabs_empty;
    cache.slabs_empty = null;

    while (curr) |c| {
        const next = c.next;
        _ = vmm_if.free(cache.root, @intFromPtr(c), SLAB_PAGE_SIZE);
        curr = next;
    }
    return 0;
}

pub fn destroyCache(cache_opaque: ?*anyopaque) callconv(.c) c_int {
    const cache = asCache(cache_opaque) orelse return abi.EINVAL;
    if (!cache.is_allocated) return abi.EINVAL;

    const root = cache.root;
    cache.is_allocated = false;

    var curr = cache.slabs_full;
    while (curr) |c| {
        const next = c.next;
        _ = vmm_if.free(root, @intFromPtr(c), SLAB_PAGE_SIZE);
        curr = next;
    }
    cache.slabs_full = null;

    curr = cache.slabs_partial;
    while (curr) |c| {
        const next = c.next;
        _ = vmm_if.free(root, @intFromPtr(c), SLAB_PAGE_SIZE);
        curr = next;
    }
    cache.slabs_partial = null;

    _ = shrink(@ptrCast(cache));

    // The C original never released the cache descriptor's own page
    // (allocated at the top of createCache) -- a one-page leak on every
    // single destroy. Free it last, now that every slab it owned is gone.
    // (Callers must not touch the handle after this returns -- a real
    // fix for the leak necessarily means the pointer is genuinely dead
    // afterward, not just logically retired.)
    _ = vmm_if.free(root, @intFromPtr(cache), SLAB_PAGE_SIZE);
    return 0;
}

comptime {
    abi.exportInterface("slab", abi.Slab, .{
        .create_cache = createCache,
        .destroy_cache = destroyCache,
        .alloc = alloc,
        .free = free,
        .shrink = shrink,
    });
}

comptime {
    _ = @import("test.zig");
}
