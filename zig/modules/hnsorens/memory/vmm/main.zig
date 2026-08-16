//! Virtual memory manager, exporting `Vmm` (category "vmm"). Address
//! spaces are tracked as an intrusive BST of VMA (virtual memory area)
//! nodes sorted by base address; VMA and space descriptor memory itself
//! comes from a small bootstrap slab-of-slabs allocated directly via pmm
//! (never through heap/slab, which both depend on vmm -- that would be
//! circular).
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

const pmm_if = abi.importInterfaceAny(abi.Pmm);
const mmu_if = abi.importInterfaceAny(abi.Mmu);
const serial_if = abi.importInterfaceAny(abi.Serial);

const VMM_USER_SPACE_MIN: u64 = 0x1000;
const VMM_USER_SPACE_MAX: u64 = 0x00007FFFFFFFF000;
const VMM_DEFAULT_ALIGNMENT: u64 = 4096;

fn alignUp(x: u64) u64 {
    return (x + (VMM_DEFAULT_ALIGNMENT - 1)) & ~(VMM_DEFAULT_ALIGNMENT - 1);
}

const VmArea = extern struct {
    base: u64 = 0,
    size: u64 = 0,
    flags: u64 = 0,
    region_type: abi.RegionType = .free,
    is_paged: bool = false,
    in_use: bool = false,
    /// Tree pointer alias: right child while linked into a space's BST.
    next: ?*VmArea = null,
    /// Tree pointer alias: left child while linked into a space's BST.
    prev: ?*VmArea = null,
};

const VmmSpace = extern struct {
    page_table_root: u64 = 0,
    mmap_cache: ?*VmArea = null,
    vma_head: ?*VmArea = null,
    next: ?*VmmSpace = null,
    in_use: bool = false,
};

const BootRegion = struct {
    base: u64,
    size: u64,
    flags: u64,
    region_type: abi.RegionType,
};

// --- Bootstrap slab-of-slabs allocators (VMA / VmmSpace descriptors) ------
//
// Each slab is exactly one physical page. The C original computed its
// per-page element count as `4096 / sizeof(element)`, which silently
// assumes the slab header (next pointer + free count) happens to fit in
// the division's remainder -- true by luck for its particular struct
// layout, but not guaranteed. Compute the header size explicitly and
// subtract it first, so the element count is correct regardless of
// VmArea/VmmSpace's actual size.

fn SlabOf(comptime T: type) type {
    const Header = extern struct {
        next: ?*anyopaque = null,
        free_count: u64 = 0,
    };
    const per_page = (4096 - @sizeOf(Header)) / @sizeOf(T);
    return extern struct {
        const Self = @This();
        const PER_PAGE = per_page;

        next: ?*Self = null,
        free_count: u64 = 0,
        storage: [per_page]T = undefined,
    };
}

const VmaSlab = SlabOf(VmArea);
const VmmSpaceSlab = SlabOf(VmmSpace);

var s_space_list_head: ?*VmmSpace = null;

var s_slab_head: ?*VmaSlab = null;
var s_vma_free_list: ?*VmArea = null;

var s_space_slab_head: ?*VmmSpaceSlab = null;
var s_space_free_list: ?*VmmSpace = null;

var g_kernel_space_root: u64 = 0;

fn vmaAlloc() ?*VmArea {
    if (s_vma_free_list == null) {
        var slab_phys: u64 = undefined;
        if (pmm_if.alloc_page(0, &slab_phys) != 0) return null;
        const slab: *VmaSlab = @ptrFromInt(slab_phys + abi.HHDM_OFFSET);
        @memset(std.mem.asBytes(slab), 0);
        slab.next = s_slab_head;
        slab.free_count = VmaSlab.PER_PAGE;
        s_slab_head = slab;

        for (0..VmaSlab.PER_PAGE - 1) |i| slab.storage[i].next = &slab.storage[i + 1];
        slab.storage[VmaSlab.PER_PAGE - 1].next = null;
        s_vma_free_list = &slab.storage[0];
    }

    const vma = s_vma_free_list.?;
    s_vma_free_list = vma.next;
    @memset(std.mem.asBytes(vma), 0);
    vma.in_use = true;
    return vma;
}

fn vmaFree(vma: *VmArea) void {
    vma.in_use = false;
    vma.next = s_vma_free_list;
    s_vma_free_list = vma;
}

fn vmmSpaceMetaAlloc() ?*VmmSpace {
    if (s_space_free_list == null) {
        var slab_phys: u64 = undefined;
        if (pmm_if.alloc_page(0, &slab_phys) != 0) return null;
        const slab: *VmmSpaceSlab = @ptrFromInt(slab_phys + abi.HHDM_OFFSET);
        @memset(std.mem.asBytes(slab), 0);
        slab.next = s_space_slab_head;
        slab.free_count = VmmSpaceSlab.PER_PAGE;
        s_space_slab_head = slab;

        for (0..VmmSpaceSlab.PER_PAGE - 1) |i| slab.storage[i].next = &slab.storage[i + 1];
        slab.storage[VmmSpaceSlab.PER_PAGE - 1].next = null;
        s_space_free_list = &slab.storage[0];
    }

    const space = s_space_free_list.?;
    s_space_free_list = space.next;
    @memset(std.mem.asBytes(space), 0);
    space.in_use = true;
    return space;
}

fn vmmSpaceMetaFree(space: *VmmSpace) void {
    space.in_use = false;
    space.next = s_space_free_list;
    s_space_free_list = space;
}

/// Looks up the tracking space for `root`, or -- for a root that was never
/// registered via `space_create` (e.g. obtained directly from
/// `mmu.get_kernel_ctx`/`mmu.alloc`) -- transparently creates and
/// registers one. Other modules' tests rely on this fallback.
fn getSpace(root: u64) ?*VmmSpace {
    var curr = s_space_list_head;
    while (curr) |c| : (curr = c.next) {
        if (c.in_use and c.page_table_root == root) return c;
    }

    const new_space = vmmSpaceMetaAlloc() orelse return null;
    new_space.page_table_root = root;
    new_space.vma_head = null;
    new_space.mmap_cache = null;

    // Zero the page table root memory to make it a valid empty L0 table.
    const l0: [*]u8 = @ptrFromInt(root + abi.HHDM_OFFSET);
    @memset(l0[0..4096], 0);

    new_space.next = s_space_list_head;
    s_space_list_head = new_space;
    return new_space;
}

// --- O(log N) BST operations -----------------------------------------------

fn findVma(space: *VmmSpace, addr: u64) ?*VmArea {
    if (space.mmap_cache) |c| {
        if (addr >= c.base and addr < c.base + c.size) return c;
    }

    var curr = space.vma_head;
    while (curr) |c| {
        if (addr >= c.base and addr < c.base + c.size) {
            space.mmap_cache = c;
            return c;
        }
        curr = if (addr < c.base) c.prev else c.next;
    }
    return null;
}

fn insertVma(space: *VmmSpace, vma: *VmArea) void {
    vma.prev = null;
    vma.next = null;

    if (space.vma_head == null) {
        space.vma_head = vma;
        return;
    }

    var curr = space.vma_head.?;
    while (true) {
        if (vma.base < curr.base) {
            if (curr.prev) |p| {
                curr = p;
            } else {
                curr.prev = vma;
                break;
            }
        } else {
            if (curr.next) |n| {
                curr = n;
            } else {
                curr.next = vma;
                break;
            }
        }
    }
}

fn removeTreeNode(space: *VmmSpace, target: *VmArea) void {
    if (space.vma_head == null) return;

    var parent: ?*VmArea = null;
    var curr: ?*VmArea = space.vma_head;
    while (curr) |c| {
        if (c == target) break;
        parent = c;
        curr = if (target.base < c.base) c.prev else c.next;
    }
    if (curr == null) return;

    var node_to_free = target;

    if (target.prev == null or target.next == null) {
        const child = target.prev orelse target.next;
        if (parent) |p| {
            if (p.prev == target) p.prev = child else p.next = child;
        } else {
            space.vma_head = child;
        }
    } else {
        // Two children: find the in-order successor (leftmost node of the
        // right subtree), copy its data into `target`, then unlink and
        // free the successor node instead.
        var succ_parent = target;
        var successor = target.next.?;
        while (successor.prev) |p| {
            succ_parent = successor;
            successor = p;
        }

        target.base = successor.base;
        target.size = successor.size;
        target.flags = successor.flags;
        target.region_type = successor.region_type;
        target.is_paged = successor.is_paged;

        if (succ_parent.prev == successor) {
            succ_parent.prev = successor.next;
        } else {
            succ_parent.next = successor.next;
        }
        node_to_free = successor;
    }

    vmaFree(node_to_free);
    space.mmap_cache = null;
}

fn findUnmappedArea(space: *VmmSpace, hint: u64, sz: u64) u64 {
    if (sz == 0) return 0;

    var addr = hint;
    var max_limit: u64 = VMM_USER_SPACE_MAX;

    if (addr < VMM_USER_SPACE_MIN) addr = VMM_USER_SPACE_MIN;

    if (hint >= 0xFFFF800000000000) {
        addr = hint;
        max_limit = 0xFFFFFFFFFFFFFFFF;
    }

    addr = alignUp(addr);

    while (true) {
        if (addr > max_limit or (max_limit - addr + 1) < sz) return 0;

        var conflict: ?*VmArea = null;
        var probe = addr;
        while (probe < addr + sz) : (probe += VMM_DEFAULT_ALIGNMENT) {
            conflict = findVma(space, probe);
            if (conflict != null) break;
        }

        const c = conflict orelse return addr;
        addr = alignUp(c.base + c.size);
    }
}

// --- Public API --------------------------------------------------------------

fn spaceCreate(out_table_root: *u64) callconv(.c) c_int {
    var root: u64 = 0;
    const status = mmu_if.alloc(&root);
    if (status != 0) return status;

    const space = vmmSpaceMetaAlloc() orelse {
        _ = mmu_if.free(root);
        return abi.ENOMEM;
    };

    space.page_table_root = root;
    space.vma_head = null;
    space.mmap_cache = null;
    space.next = s_space_list_head;
    s_space_list_head = space;

    out_table_root.* = root;
    return 0;
}

fn spaceDestroy(table_root: u64) callconv(.c) c_int {
    if (table_root == 0) return abi.EINVAL;

    var prev: ?*VmmSpace = null;
    var curr: ?*VmmSpace = s_space_list_head;
    while (curr) |c| {
        if (c.page_table_root == table_root) break;
        prev = c;
        curr = c.next;
    }
    const target = curr orelse return abi.EINVAL;

    while (target.vma_head) |head| removeTreeNode(target, head);
    target.mmap_cache = null;

    if (prev) |p| {
        p.next = target.next;
    } else {
        s_space_list_head = target.next;
    }

    vmmSpaceMetaFree(target);
    return mmu_if.free(table_root);
}

fn allocate(root: u64, vaddr: *u64, sz_in: u64, flags: u64, region_type: abi.RegionType) callconv(.c) c_int {
    if (root == 0 or sz_in == 0) return abi.EINVAL;

    const space = getSpace(root) orelse return abi.EINVAL;
    const sz = alignUp(sz_in);

    if (vaddr.* >= 0x000000F000000000 or vaddr.* >= 0xFFFF800000000000) {
        if (findVma(space, vaddr.*)) |existing| {
            if (existing.region_type == region_type and existing.size >= sz) {
                vaddr.* = existing.base;
                return 0;
            }
        }
    }

    const target_addr = findUnmappedArea(space, vaddr.*, sz);
    if (target_addr == 0) return abi.ENOMEM;

    const vma = vmaAlloc() orelse return abi.ENOMEM;
    vma.base = target_addr;
    vma.size = sz;
    vma.flags = flags;
    vma.region_type = region_type;
    vma.is_paged = true;

    var i: u64 = 0;
    while (i < sz) : (i += VMM_DEFAULT_ALIGNMENT) {
        var phys_page: u64 = undefined;
        if (pmm_if.alloc_page(0, &phys_page) != 0) break;
        if (mmu_if.map(root, target_addr + i, phys_page, 1, .ps_4kb, flags) != 0) {
            _ = pmm_if.release(phys_page);
            break;
        }
    }

    if (i < sz) {
        // Partial failure: `vma` was never inserted into the tree, so
        // `free()` (which locates the range via find_vma) can't be used
        // to unwind it -- the C original tried exactly that, and since
        // find_vma always returned null, it silently did nothing, leaking
        // every page mapped so far. Walk them down directly instead.
        var j: u64 = 0;
        while (j < i) : (j += VMM_DEFAULT_ALIGNMENT) {
            var phys: u64 = 0;
            var f: u64 = 0;
            if (mmu_if.translate(root, target_addr + j, &phys, &f) == 0) {
                _ = mmu_if.unmap(root, target_addr + j, 1, .ps_4kb);
                _ = pmm_if.release(phys);
            }
        }
        vmaFree(vma);
        return abi.ENOMEM;
    }

    insertVma(space, vma);
    space.mmap_cache = vma;
    vaddr.* = vma.base;
    return 0;
}

fn reserve(root: u64, vaddr: u64, sz_in: u64) callconv(.c) c_int {
    if (root == 0 or vaddr == 0 or sz_in == 0) return abi.EINVAL;

    const space = getSpace(root) orelse return abi.EINVAL;
    const sz = alignUp(sz_in);

    if (findVma(space, vaddr) != null or findVma(space, vaddr + sz - 1) != null) {
        return abi.EEXIST;
    }

    const vma = vmaAlloc() orelse return abi.ENOMEM;
    vma.base = vaddr;
    vma.size = sz;
    vma.flags = 0;
    vma.region_type = .guard;
    vma.is_paged = false;

    insertVma(space, vma);
    return 0;
}

fn free(root: u64, vaddr: u64, sz_in: u64) callconv(.c) c_int {
    const space = getSpace(root) orelse return abi.EINVAL;

    const vma = findVma(space, vaddr) orelse return abi.EINVAL;
    if (vaddr < vma.base or (vaddr + sz_in) > (vma.base + vma.size)) return abi.EINVAL;

    const sz = alignUp(sz_in);

    if (vma.is_paged) {
        var i: u64 = 0;
        while (i < sz) : (i += VMM_DEFAULT_ALIGNMENT) {
            var phys: u64 = 0;
            var f: u64 = 0;
            if (mmu_if.translate(root, vaddr + i, &phys, &f) == 0) {
                _ = mmu_if.unmap(root, vaddr + i, 1, .ps_4kb);
                _ = pmm_if.release(phys);
            }
        }
    }

    if (vaddr == vma.base and sz == vma.size) {
        removeTreeNode(space, vma);
    } else if (vaddr == vma.base) {
        vma.base += sz;
        vma.size -= sz;
    } else if ((vaddr + sz) == (vma.base + vma.size)) {
        vma.size -= sz;
    } else {
        const split_vma = vmaAlloc() orelse return abi.ENOMEM;
        split_vma.base = vaddr + sz;
        split_vma.size = (vma.base + vma.size) - split_vma.base;
        split_vma.flags = vma.flags;
        split_vma.region_type = vma.region_type;
        split_vma.is_paged = vma.is_paged;

        vma.size = vaddr - vma.base;
        insertVma(space, split_vma);
    }

    space.mmap_cache = null;
    return 0;
}

fn resize(root: u64, vaddr: u64, old_sz: u64, new_sz_in: u64) callconv(.c) c_int {
    const space = getSpace(root) orelse return abi.EINVAL;

    const vma = findVma(space, vaddr) orelse return abi.EINVAL;
    if (vma.base != vaddr or vma.size != old_sz) return abi.EINVAL;

    const new_sz = alignUp(new_sz_in);
    if (new_sz == old_sz) return 0;

    if (new_sz < old_sz) {
        return free(root, vaddr + new_sz, old_sz - new_sz);
    }

    if (findVma(space, vaddr + old_sz) != null or (vaddr + new_sz) > VMM_USER_SPACE_MAX) {
        return abi.ENOMEM;
    }

    var i: u64 = old_sz;
    while (i < new_sz) : (i += VMM_DEFAULT_ALIGNMENT) {
        var phys_page: u64 = undefined;
        if (pmm_if.alloc_page(0, &phys_page) != 0) break;
        if (mmu_if.map(root, vaddr + i, phys_page, 1, .ps_4kb, vma.flags) != 0) {
            _ = pmm_if.release(phys_page);
            break;
        }
    }

    if (i < new_sz) {
        // Same leak class as allocate()'s cleanup: unwind whatever got
        // mapped during this partial expansion.
        var j: u64 = old_sz;
        while (j < i) : (j += VMM_DEFAULT_ALIGNMENT) {
            var phys: u64 = 0;
            var f: u64 = 0;
            if (mmu_if.translate(root, vaddr + j, &phys, &f) == 0) {
                _ = mmu_if.unmap(root, vaddr + j, 1, .ps_4kb);
                _ = pmm_if.release(phys);
            }
        }
        return abi.ENOMEM;
    }

    vma.size = new_sz;
    space.mmap_cache = vma;
    return 0;
}

fn mapExternal(root: u64, v: u64, p: u64, sz_in: u64, f: u64) callconv(.c) c_int {
    const space = getSpace(root) orelse return abi.EINVAL;
    const sz = alignUp(sz_in);

    if (findVma(space, v) != null or findVma(space, v + sz - 1) != null) {
        return abi.EEXIST;
    }

    const vma = vmaAlloc() orelse return abi.ENOMEM;
    vma.base = v;
    vma.size = sz;
    vma.flags = f;
    vma.region_type = .mmio;
    vma.is_paged = false;

    const status = mmu_if.map(root, v, p, sz / VMM_DEFAULT_ALIGNMENT, .ps_4kb, f);
    if (status != 0) {
        vmaFree(vma);
        return status;
    }

    insertVma(space, vma);
    space.mmap_cache = vma;
    return 0;
}

fn protect(root: u64, vaddr: u64, sz_in: u64, new_flags: u64) callconv(.c) c_int {
    const space = getSpace(root) orelse return abi.EINVAL;

    const vma = findVma(space, vaddr) orelse return abi.EINVAL;
    if (vaddr < vma.base or (vaddr + sz_in) > (vma.base + vma.size)) return abi.EINVAL;

    const sz = alignUp(sz_in);

    const status = mmu_if.protect(root, vaddr, sz / VMM_DEFAULT_ALIGNMENT, .ps_4kb, new_flags);
    if (status != 0) return status;

    if (vaddr == vma.base and sz == vma.size) {
        vma.flags = new_flags;
    } else if (vaddr == vma.base) {
        const split_node = vmaAlloc() orelse return abi.ENOMEM;
        split_node.base = vaddr;
        split_node.size = sz;
        split_node.flags = new_flags;
        split_node.region_type = vma.region_type;
        split_node.is_paged = vma.is_paged;

        vma.base += sz;
        vma.size -= sz;
        insertVma(space, split_node);
    } else if ((vaddr + sz) == (vma.base + vma.size)) {
        const split_node = vmaAlloc() orelse return abi.ENOMEM;
        split_node.base = vaddr;
        split_node.size = sz;
        split_node.flags = new_flags;
        split_node.region_type = vma.region_type;
        split_node.is_paged = vma.is_paged;

        vma.size -= sz;
        insertVma(space, split_node);
    } else {
        // 3-way split: left (unchanged) / mid (new flags) / right (unchanged).
        const mid_node = vmaAlloc() orelse return abi.ENOMEM;
        const right_node = vmaAlloc() orelse {
            vmaFree(mid_node);
            return abi.ENOMEM;
        };

        const original_right_base = vaddr + sz;
        const original_right_size = (vma.base + vma.size) - original_right_base;

        mid_node.base = vaddr;
        mid_node.size = sz;
        mid_node.flags = new_flags;
        mid_node.region_type = vma.region_type;
        mid_node.is_paged = vma.is_paged;

        right_node.base = original_right_base;
        right_node.size = original_right_size;
        right_node.flags = vma.flags;
        right_node.region_type = vma.region_type;
        right_node.is_paged = vma.is_paged;

        const original_left_base = vma.base;
        const original_left_size = vaddr - vma.base;
        const original_left_flags = vma.flags;
        const original_left_type = vma.region_type;
        const original_left_paged = vma.is_paged;

        // Removes `vma` from the tree (and frees its node) before the
        // left replacement is inserted -- matches the C original's order,
        // which avoids ever touching the (possibly reused) freed node
        // again.
        removeTreeNode(space, vma);

        const left_node = vmaAlloc() orelse {
            vmaFree(mid_node);
            vmaFree(right_node);
            return abi.ENOMEM;
        };
        left_node.base = original_left_base;
        left_node.size = original_left_size;
        left_node.flags = original_left_flags;
        left_node.region_type = original_left_type;
        left_node.is_paged = original_left_paged;

        insertVma(space, left_node);
        insertVma(space, mid_node);
        insertVma(space, right_node);
    }

    space.mmap_cache = null;
    return 0;
}

fn query(root: u64, vaddr: u64, out_info: *abi.VmmRegionInfo) callconv(.c) c_int {
    const space = getSpace(root) orelse return abi.EINVAL;
    const vma = findVma(space, vaddr) orelse return abi.EINVAL;

    out_info.* = .{
        .base = vma.base,
        .size = vma.size,
        .flags = vma.flags,
        .region_type = vma.region_type,
        .is_paged = vma.is_paged,
    };
    return 0;
}

fn activate(root: u64) callconv(.c) c_int {
    return mmu_if.set_user_ctx(root, 1);
}

fn sync(root: u64, vaddr: u64, sz: u64) callconv(.c) c_int {
    _ = root;
    const pages = (sz + VMM_DEFAULT_ALIGNMENT - 1) / VMM_DEFAULT_ALIGNMENT;
    return mmu_if.invalidate(vaddr, pages, .ps_4kb);
}

comptime {
    abi.exportInterface("vmm", abi.Vmm, .{
        .space_create = spaceCreate,
        .space_destroy = spaceDestroy,
        .allocate = allocate,
        .reserve = reserve,
        .free = free,
        .resize = resize,
        .map_external = mapExternal,
        .protect = protect,
        .query = query,
        .activate = activate,
        .sync = sync,
    });
}

// --- Bootstrap (module entry point) -----------------------------------------

fn spaceCreateWithState(boot_pt_root: u64, regions: []const BootRegion, out_space_root: *u64) c_int {
    if (boot_pt_root == 0) return abi.EINVAL;

    const space = vmmSpaceMetaAlloc() orelse return abi.ENOMEM;
    space.page_table_root = boot_pt_root;
    space.vma_head = null;
    space.mmap_cache = null;

    for (regions) |r| {
        const vma = vmaAlloc() orelse {
            while (space.vma_head) |head| removeTreeNode(space, head);
            vmmSpaceMetaFree(space);
            return abi.ENOMEM;
        };
        vma.base = r.base;
        vma.size = r.size;
        vma.flags = r.flags;
        vma.region_type = r.region_type;
        vma.is_paged = true;
        insertVma(space, vma);
    }

    space.next = s_space_list_head;
    s_space_list_head = space;

    out_space_root.* = boot_pt_root;
    return 0;
}

fn init(boot_pt_root: u64, regions: []const BootRegion) c_int {
    if (boot_pt_root == 0) return abi.EINVAL;

    s_space_list_head = null;
    s_slab_head = null;
    s_vma_free_list = null;
    s_space_slab_head = null;
    s_space_free_list = null;

    return spaceCreateWithState(boot_pt_root, regions, &g_kernel_space_root);
}

pub fn main(boot_info_ptr: *anyopaque) void {
    const boot_info: *abi.BootInfo = @ptrCast(@alignCast(boot_info_ptr));

    const region = BootRegion{
        .base = 0,
        .size = boot_info.virtual_start,
        .flags = 0x3,
        .region_type = .data,
    };

    var kernel_table_root: u64 = undefined;
    _ = mmu_if.get_kernel_ctx(&kernel_table_root);

    _ = init(kernel_table_root, &[_]BootRegion{region});
}

// --- Unit tests --------------------------------------------------------------

fn testSpaceLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = 0;
    t.expectEqual(@src(), spaceCreate(&root), 0);
    t.expectNotEqual(@src(), root, 0);
    t.expectEqual(@src(), spaceDestroy(root), 0);

    // The C original also checked `vmm_space_create(NULL)` here; that
    // invariant (out_table_root is never null) is enforced at the type
    // level in this port (a non-optional `*u64`), so there's no runtime
    // path left to exercise -- constructing a null value for it would
    // itself trip Zig's own safety checks.
    t.expectEqual(@src(), spaceDestroy(0), abi.EINVAL);
    return t.result();
}

fn testAllocationAndQuery() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const requested_sz: u64 = 4096 * 3;
    t.expectEqual(@src(), allocate(root, &vaddr, requested_sz, abi.MMU_USER, .data), 0);
    t.expectNotEqual(@src(), vaddr, 0);

    var info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr + 4096, &info), 0);
    t.expectEqual(@src(), info.base, vaddr);
    t.expectEqual(@src(), info.size, 4096 * 3);
    t.expectEqual(@src(), info.region_type, .data);
    t.expectEqual(@src(), info.is_paged, true);

    t.expectNotEqual(@src(), query(root, vaddr + 4096 * 10, &info), 0);

    t.expectEqual(@src(), spaceDestroy(root), 0);
    return t.result();
}

fn testStackGuardLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var stack_vaddr: u64 = 0x00007FFFF0000000;
    const stack_sz: u64 = 4096 * 4;
    const guard_vaddr = stack_vaddr - 4096;

    t.expectEqual(@src(), reserve(root, guard_vaddr, 4096), 0);
    t.expectEqual(@src(), allocate(root, &stack_vaddr, stack_sz, abi.MMU_USER, .stack), 0);

    var guard_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, guard_vaddr, &guard_info), 0);
    t.expectEqual(@src(), guard_info.region_type, .guard);
    t.expectEqual(@src(), guard_info.size, 4096);
    t.expectEqual(@src(), guard_info.is_paged, false);

    t.expectEqual(@src(), free(root, stack_vaddr, stack_sz), 0);
    t.expectEqual(@src(), free(root, guard_vaddr, 4096), 0);

    t.expectNotEqual(@src(), query(root, guard_vaddr, &guard_info), 0);

    t.expectEqual(@src(), spaceDestroy(root), 0);
    return t.result();
}

fn testFreeSubrangeSplitting() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const total_sz: u64 = 4096 * 5;
    t.expectEqual(@src(), allocate(root, &vaddr, total_sz, abi.MMU_USER, .data), 0);

    const punch_vaddr = vaddr + 4096 * 2;
    t.expectEqual(@src(), free(root, punch_vaddr, 4096), 0);

    var left_chunk: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr, &left_chunk), 0);
    t.expectEqual(@src(), left_chunk.size, 4096 * 2);

    var right_chunk: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr + 4096 * 3, &right_chunk), 0);
    t.expectEqual(@src(), right_chunk.base, vaddr + 4096 * 3);
    t.expectEqual(@src(), right_chunk.size, 4096 * 2);

    var middle_hole: abi.VmmRegionInfo = undefined;
    t.expectNotEqual(@src(), query(root, punch_vaddr, &middle_hole), 0);

    t.expectEqual(@src(), spaceDestroy(root), 0);
    return t.result();
}

fn testDynamicResizing() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    t.expectEqual(@src(), allocate(root, &vaddr, 4096 * 2, abi.MMU_USER, .data), 0);

    t.expectEqual(@src(), resize(root, vaddr, 4096 * 2, 4096 * 5), 0);
    var expand_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr, &expand_info), 0);
    t.expectEqual(@src(), expand_info.size, 4096 * 5);

    t.expectEqual(@src(), resize(root, vaddr, 4096 * 5, 4096 * 1), 0);
    var shrink_info: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr, &shrink_info), 0);
    t.expectEqual(@src(), shrink_info.size, 4096 * 1);

    var blocker_vaddr = vaddr + 4096;
    t.expectEqual(@src(), allocate(root, &blocker_vaddr, 4096, abi.MMU_USER, .data), 0);

    t.expectEqual(@src(), resize(root, vaddr, 4096 * 1, 4096 * 3), abi.ENOMEM);

    t.expectEqual(@src(), spaceDestroy(root), 0);
    return t.result();
}

fn testProtectFragmentation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var vaddr: u64 = 0x00007FFFF0000000;
    const total_sz: u64 = 4096 * 3;
    t.expectEqual(@src(), allocate(root, &vaddr, total_sz, abi.MMU_USER, .data), 0);

    const mid_vaddr = vaddr + 4096;
    t.expectEqual(@src(), protect(root, mid_vaddr, 4096, abi.MMU_RO | abi.MMU_USER), 0);

    var left: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr, &left), 0);
    t.expectEqual(@src(), left.size, 4096);
    t.expectEqual(@src(), left.flags, abi.MMU_USER);

    var mid: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, mid_vaddr, &mid), 0);
    t.expectEqual(@src(), mid.size, 4096);
    t.expectEqual(@src(), mid.flags, abi.MMU_RO | abi.MMU_USER);

    var right: abi.VmmRegionInfo = undefined;
    t.expectEqual(@src(), query(root, vaddr + 4096 * 2, &right), 0);
    t.expectEqual(@src(), right.size, 4096);
    t.expectEqual(@src(), right.flags, abi.MMU_USER);

    t.expectEqual(@src(), spaceDestroy(root), 0);
    return t.result();
}

fn testExhaustionLimits() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const MAX_VMA_POOL_SIZE = 512;

    var root: u64 = undefined;
    t.expectEqual(@src(), spaceCreate(&root), 0);

    var allocated: [MAX_VMA_POOL_SIZE + 5]u64 = undefined;
    var allocation_count: usize = 0;

    for (0..MAX_VMA_POOL_SIZE + 2) |_| {
        var hint: u64 = 0;
        const status = allocate(root, &hint, 4096, 0x713, .data);
        if (status == 0) {
            allocated[allocation_count] = hint;
            allocation_count += 1;
        } else {
            t.expectEqual(@src(), status, abi.ENOMEM);
            break;
        }
    }

    for (0..allocation_count) |i| {
        t.expectEqual(@src(), free(root, allocated[i], 4096), 0);
    }

    t.expectEqual(@src(), spaceDestroy(root), 0);
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
}
