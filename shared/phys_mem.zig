//! Tiny helper for allocating physically-contiguous, HHDM-mapped scratch
//! buffers via an imported `Pmm`. Used throughout the storage/filesystem
//! stack (virtio_bus, virtio_blk, gpt, ext2) for every buffer that might be
//! handed to a VirtIO device as a DMA source/destination -- `Pmm.alloc_page`
//! guarantees physical contiguity and a fixed `phys + HHDM_OFFSET` virtual
//! alias, which a general-purpose heap (backed by arbitrary, possibly
//! non-contiguous VMM-mapped pages) does not. This mirrors the C reference
//! drivers always allocating scratch buffers through their `pmm_alloc_phys`
//! equivalent rather than a general kmalloc.
const abi = @import("abi");

fn orderForBytes(size: u64) u8 {
    const pages = (size + 4095) / 4096;
    var order: u8 = 0;
    while ((@as(u64, 1) << @as(u6, @intCast(order))) < pages) : (order += 1) {}
    return order;
}

/// Allocates at least `size` bytes of physically-contiguous memory.
/// On success, writes its HHDM virtual address (for the caller to read/
/// write) to `out_virt` and its physical address (for programming into a
/// device) to `out_phys`.
pub fn alloc(pmm_if: *const abi.Pmm, size: u64, out_virt: *u64, out_phys: *u64) c_int {
    var phys: u64 = undefined;
    const status = pmm_if.alloc_page(orderForBytes(size), &phys);
    if (status != 0) return status;
    out_phys.* = phys;
    out_virt.* = phys + abi.HHDM_OFFSET;
    return 0;
}

/// Releases a buffer previously returned by `alloc`, given its physical
/// address (`out_phys` from that call, or `virtToPhys` of its virtual one).
pub fn free(pmm_if: *const abi.Pmm, phys: u64) void {
    _ = pmm_if.release(phys);
}

pub fn virtToPhys(virt: u64) u64 {
    return virt - abi.HHDM_OFFSET;
}

pub fn physToVirt(phys: u64) u64 {
    return phys + abi.HHDM_OFFSET;
}
