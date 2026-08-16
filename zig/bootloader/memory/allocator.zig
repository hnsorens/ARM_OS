const std = @import("std");
const uefi = std.os.uefi;
const MemoryType = uefi.tables.MemoryType;

pub const UefiAllocMode = enum {
    pool,
    pages
};

pub fn UefiPoolAllocator(comptime pool_type: MemoryType, comptime mode: UefiAllocMode) type {
    return struct {
        const Self = @This();

        const vtable = std.mem.Allocator.VTable{
            .alloc = alloc,
            .free = free,
            .resize = resize,
            .remap = remap,
        };

        pub fn allocator() std.mem.Allocator {
            return .{
                .ptr = undefined,
                .vtable = &vtable
            };
        }


        fn remap(
            ctx: *anyopaque,
            buf: []u8,
            buf_align: std.mem.Alignment,
            new_len: usize,
            ret_addr: usize,
        ) ?[*]u8 {
            _ = ctx;
            _ = buf;
            _ = buf_align;
            _ = new_len;
            _ = ret_addr;
            return null;
        }

        fn alloc(
            ctx: *anyopaque,
            len: usize,
            ptr_align: std.mem.Alignment,
            ret_addr: usize
        ) ?[*]u8 {
            _ = ctx;
            _ = ptr_align;
            _ = ret_addr;

            const bs = uefi.system_table.boot_services.?;

            switch (mode) {
                .pool => {
                    const raw_ptr = bs.allocatePool(pool_type, len) catch {
                        return null;
                    };
                    return raw_ptr.ptr;
                },
                .pages => {
                    const page_count: usize = (len + 4095) / 4096;

                    const pages = bs.allocatePages(.any, pool_type, page_count) catch {
                        return null;
                    };
                    return @ptrCast(pages.ptr);
                },
            }
        }

        fn resize(
            ctx: *anyopaque,
            buf: []u8,
            buf_align: std.mem.Alignment,
            new_len: usize,
            ret_addr: usize
        ) bool {
            _ = ctx;
            _ = buf_align;
            _ = ret_addr;
            return new_len <= buf.len;
        }

        fn free(
            ctx: *anyopaque,
            buf: []u8,
            buf_align: std.mem.Alignment,
            ret_addr: usize
        ) void {
            _ = ctx;
            _ = buf_align;
            _ = ret_addr;

            const bs = uefi.system_table.boot_services.?;

            switch (mode) {
                .pool => {
                    _ = bs.freePool(@ptrCast(@alignCast(buf.ptr))) catch {
                        return;
                    };
                },
                .pages => {
                    const page_count: usize = (buf.len + 4095) / 4096;
                    const pages_ptr: [*]align(4096) uefi.Page = @ptrCast(@alignCast(buf.ptr));
                    bs.freePages(pages_ptr[0..page_count]) catch {};
                },
            }
        }
    };
}

/// A plain bump allocator over a caller-owned block. Unlike
/// `UefiPoolAllocator`, this never calls back into UEFI boot services, so
/// it's the one to use for anything (like the module registry) that keeps
/// allocating *after* ExitBootServices -- boot services are gone by then,
/// and calling through a stale function table is a fault waiting to happen.
/// Never frees; matches the C bootloader's fixed `ModuleRegistryBlock`.
pub const BumpAllocator = struct {
    buffer: []u8,
    offset: usize = 0,

    const vtable = std.mem.Allocator.VTable{
        .alloc = alloc,
        .free = free,
        .resize = resize,
        .remap = remap,
    };

    pub fn allocator(self: *BumpAllocator) std.mem.Allocator {
        return .{ .ptr = self, .vtable = &vtable };
    }

    fn alloc(ctx: *anyopaque, len: usize, ptr_align: std.mem.Alignment, ret_addr: usize) ?[*]u8 {
        _ = ret_addr;
        const self: *BumpAllocator = @ptrCast(@alignCast(ctx));

        // Align the actual pointer value, not just the offset: the offset
        // being a multiple of the alignment only makes the resulting
        // pointer aligned if `buffer.ptr` itself already is, which isn't
        // guaranteed (e.g. a plain byte array on the stack).
        const base = @intFromPtr(self.buffer.ptr);
        const aligned_addr = ptr_align.forward(base + self.offset);
        const aligned_offset = aligned_addr - base;

        if (aligned_offset + len > self.buffer.len) return null;
        self.offset = aligned_offset + len;
        return @ptrFromInt(aligned_addr);
    }

    fn resize(ctx: *anyopaque, buf: []u8, buf_align: std.mem.Alignment, new_len: usize, ret_addr: usize) bool {
        _ = ctx;
        _ = buf_align;
        _ = ret_addr;
        return new_len <= buf.len;
    }

    fn remap(ctx: *anyopaque, buf: []u8, buf_align: std.mem.Alignment, new_len: usize, ret_addr: usize) ?[*]u8 {
        _ = ctx;
        _ = buf;
        _ = buf_align;
        _ = new_len;
        _ = ret_addr;
        return null;
    }

    fn free(ctx: *anyopaque, buf: []u8, buf_align: std.mem.Alignment, ret_addr: usize) void {
        _ = ctx;
        _ = buf;
        _ = buf_align;
        _ = ret_addr;
    }
};

// --- Unit tests -------------------------------------------------------
//
// `UefiPoolAllocator` isn't covered here: every one of its paths calls into
// UEFI boot services, which don't exist on the native test target. Only
// `BumpAllocator` (used post-ExitBootServices, once boot services really
// are gone) is UEFI-independent and testable natively.

const testing = std.testing;

test "BumpAllocator hands out sequential, non-overlapping allocations" {
    var backing: [64]u8 = undefined;
    var bump = BumpAllocator{ .buffer = &backing };
    const a = bump.allocator();

    const p1 = try a.alloc(u8, 10);
    const p2 = try a.alloc(u8, 10);
    defer a.free(p1);
    defer a.free(p2);

    try testing.expect(@intFromPtr(p2.ptr) >= @intFromPtr(p1.ptr) + p1.len);
}

test "BumpAllocator respects requested alignment" {
    var backing: [128]u8 = undefined;
    var bump = BumpAllocator{ .buffer = &backing };
    const a = bump.allocator();

    _ = try a.alloc(u8, 1); // misalign the offset first
    const p = try a.alignedAlloc(u8, .fromByteUnits(16), 8);
    defer a.free(p);

    try testing.expectEqual(@as(usize, 0), @intFromPtr(p.ptr) % 16);
}

test "BumpAllocator returns error.OutOfMemory once the backing buffer is exhausted" {
    var backing: [8]u8 = undefined;
    var bump = BumpAllocator{ .buffer = &backing };
    const a = bump.allocator();

    try testing.expectError(error.OutOfMemory, a.alloc(u8, 9));
    const p = try a.alloc(u8, 8);
    defer a.free(p);
    try testing.expectError(error.OutOfMemory, a.alloc(u8, 1));
}

test "BumpAllocator.resize only allows shrinking" {
    var backing: [16]u8 = undefined;
    var bump = BumpAllocator{ .buffer = &backing };
    const a = bump.allocator();

    const p = try a.alloc(u8, 8);
    defer a.free(p);

    try testing.expect(a.resize(p, 4));
    try testing.expect(!a.resize(p, 9));
}
