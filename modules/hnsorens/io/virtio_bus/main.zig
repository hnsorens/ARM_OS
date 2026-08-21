//! Generic VirtIO-MMIO transport, exporting `VirtioBus` (category
//! "virtiobus"). Ported from `things_to_add/bus_controller.c`: scans QEMU
//! virt's fixed VirtIO-MMIO window for a device by ID, runs the common
//! (device-independent) feature-negotiation handshake, and drives a single
//! split virtqueue for request/response style devices (the only pattern
//! any current or planned device here needs).
//!
//! `mmio_base` throughout is the identity-mapped physical address of a
//! device's MMIO window, used directly as a pointer -- TTBR0's flat
//! identity map (see bootloader/main.zig) covers the low physical range
//! these devices live in, same assumption the C reference's
//! `virt_to_phys(x) = x` encoded.
//!
//! The C reference set `VIRTIO_F_RING_EVENT_IDX` twice (a copy-pasted
//! duplicate block) despite `submit_request`'s polling loop never using
//! the event index at all; this port negotiates only `VIRTIO_F_VERSION_1`,
//! the one feature it actually relies on.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const mmio = @import("mmio");
const phys_mem = @import("phys_mem");

pub const pmm_if = abi.importInterface(abi.Pmm);
pub const serial_if = abi.importInterface(abi.Serial);

const mmio_scan_base = abi.declareConfigInt("mmio_scan_base", u64, 0x0A000000);
const mmio_scan_end = abi.declareConfigInt("mmio_scan_end", u64, 0x0A004000);
const mmio_scan_stride = abi.declareConfigInt("mmio_scan_stride", u64, 0x200);

const VIRTIO_MAGIC: u32 = 0x74726976;
const VIRTIO_MODERN_VERSION: u32 = 2;
const VIRTIO_F_VERSION_1_BIT: u5 = 32 - 32; // bit 32 overall, bit 0 of the high feature word

const STATUS_ACKNOWLEDGE: u32 = 0x01;
const STATUS_DRIVER: u32 = 0x02;
const STATUS_DRIVER_OK: u32 = 0x04;
const STATUS_FEATURES_OK: u32 = 0x08;

const VIRTQ_DESC_F_NEXT: u16 = 1;
const VIRTQ_DESC_F_WRITE: u16 = 2;

const NO_FREE_DESC: u16 = 0xFFFF;

const Regs = struct {
    const Magic = mmio.Reg(u32, 0x000);
    const Version = mmio.Reg(u32, 0x004);
    const DeviceId = mmio.Reg(u32, 0x008);
    const DeviceFeatures = mmio.Reg(u32, 0x010);
    const DeviceFeaturesSel = mmio.Reg(u32, 0x014);
    const DriverFeatures = mmio.Reg(u32, 0x020);
    const DriverFeaturesSel = mmio.Reg(u32, 0x024);
    const QueueSel = mmio.Reg(u32, 0x030);
    const QueueNumMax = mmio.Reg(u32, 0x034);
    const QueueNum = mmio.Reg(u32, 0x038);
    const QueueReady = mmio.Reg(u32, 0x044);
    const QueueNotify = mmio.Reg(u32, 0x050);
    const Status = mmio.Reg(u32, 0x070);
    const QueueDescLow = mmio.Reg(u32, 0x080);
    const QueueDescHigh = mmio.Reg(u32, 0x084);
    const QueueDriverLow = mmio.Reg(u32, 0x090);
    const QueueDriverHigh = mmio.Reg(u32, 0x094);
    const QueueDeviceLow = mmio.Reg(u32, 0x0a0);
    const QueueDeviceHigh = mmio.Reg(u32, 0x0a4);
};

/// One 16-byte virtqueue descriptor -- internal layout, never crosses the
/// ABI boundary (only `abi.VirtioQueue`'s pointers to a table of these do).
const VirtqDesc = extern struct {
    addr: u64 = 0,
    len: u32 = 0,
    flags: u16 = 0,
    next: u16 = 0,
};

const VirtqAvailHeader = extern struct {
    flags: u16 = 0,
    idx: u16 = 0,
};

const VirtqUsedElem = extern struct {
    id: u32 = 0,
    len: u32 = 0,
};

const VirtqUsedHeader = extern struct {
    flags: u16 = 0,
    idx: u16 = 0,
};

fn descTable(q: *const abi.VirtioQueue) [*]volatile VirtqDesc {
    return @ptrFromInt(q.desc);
}

fn availHeader(q: *const abi.VirtioQueue) *volatile VirtqAvailHeader {
    return @ptrFromInt(q.avail);
}

fn availRing(q: *const abi.VirtioQueue) [*]volatile u16 {
    return @ptrFromInt(q.avail + @sizeOf(VirtqAvailHeader));
}

fn usedHeader(q: *const abi.VirtioQueue) *volatile VirtqUsedHeader {
    return @ptrFromInt(q.used);
}

fn usedRing(q: *const abi.VirtioQueue) [*]volatile VirtqUsedElem {
    return @ptrFromInt(q.used + @sizeOf(VirtqUsedHeader));
}

fn dmb() void {
    asm volatile ("dmb sy" ::: .{ .memory = true });
}

pub fn findDevice(device_id: u32) callconv(.c) u64 {
    var base = mmio_scan_base.*;
    while (base < mmio_scan_end.*) : (base += mmio_scan_stride.*) {
        if (Regs.Magic.read(base) != VIRTIO_MAGIC) continue;
        if (Regs.DeviceId.read(base) == device_id) return base;
    }
    return 0;
}

pub fn initDevice(mmio_base: u64) callconv(.c) c_int {
    if (mmio_base == 0) return abi.EINVAL;
    if (Regs.Magic.read(mmio_base) != VIRTIO_MAGIC) return abi.EIO;
    if (Regs.Version.read(mmio_base) != VIRTIO_MODERN_VERSION) return abi.EIO;
    if (Regs.DeviceId.read(mmio_base) == 0) return abi.EIO;

    // Reset, then step through the ACKNOWLEDGE / DRIVER / FEATURES_OK
    // handshake (virtio-mmio spec section 4.2.3.1).
    Regs.Status.write(mmio_base, 0);

    var status: u32 = STATUS_ACKNOWLEDGE;
    Regs.Status.write(mmio_base, status);
    status |= STATUS_DRIVER;
    Regs.Status.write(mmio_base, status);

    Regs.DeviceFeaturesSel.write(mmio_base, 1);
    const features_high = Regs.DeviceFeatures.read(mmio_base);

    var driver_features_high: u32 = 0;
    if ((features_high >> VIRTIO_F_VERSION_1_BIT) & 1 != 0) {
        driver_features_high |= @as(u32, 1) << VIRTIO_F_VERSION_1_BIT;
    }

    Regs.DriverFeaturesSel.write(mmio_base, 0);
    Regs.DriverFeatures.write(mmio_base, 0);
    Regs.DriverFeaturesSel.write(mmio_base, 1);
    Regs.DriverFeatures.write(mmio_base, driver_features_high);

    status |= STATUS_FEATURES_OK;
    Regs.Status.write(mmio_base, status);

    if (Regs.Status.read(mmio_base) & STATUS_FEATURES_OK == 0) return abi.EIO;

    // Deliberately does not touch queues or set DRIVER_OK: those are
    // device-specific and are the calling driver's (e.g. virtio_blk's) job.
    return 0;
}

pub fn setupQueue(mmio_base: u64, queue_idx: u32, queue: *abi.VirtioQueue) callconv(.c) c_int {
    Regs.QueueSel.write(mmio_base, queue_idx);

    const max_size = Regs.QueueNumMax.read(mmio_base);
    if (max_size == 0) return abi.EIO;

    var queue_size = queue.size;
    if (queue_size == 0 or queue_size > max_size) queue_size = @truncate(max_size);
    queue.size = queue_size;

    Regs.QueueNum.write(mmio_base, queue_size);

    const desc_size: u64 = @as(u64, queue_size) * @sizeOf(VirtqDesc);
    const avail_size: u64 = @sizeOf(VirtqAvailHeader) + @as(u64, queue_size) * @sizeOf(u16);
    const used_size: u64 = @sizeOf(VirtqUsedHeader) + @as(u64, queue_size) * @sizeOf(VirtqUsedElem);

    var desc_virt: u64 = undefined;
    var desc_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, desc_size, &desc_virt, &desc_phys) != 0) return abi.ENOMEM;

    var avail_virt: u64 = undefined;
    var avail_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, avail_size, &avail_virt, &avail_phys) != 0) {
        phys_mem.free(pmm_if, desc_phys);
        return abi.ENOMEM;
    }

    var used_virt: u64 = undefined;
    var used_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, used_size, &used_virt, &used_phys) != 0) {
        phys_mem.free(pmm_if, desc_phys);
        phys_mem.free(pmm_if, avail_phys);
        return abi.ENOMEM;
    }

    const desc_bytes: [*]u8 = @ptrFromInt(desc_virt);
    @memset(desc_bytes[0..desc_size], 0);
    const avail_bytes: [*]u8 = @ptrFromInt(avail_virt);
    @memset(avail_bytes[0..avail_size], 0);
    const used_bytes: [*]u8 = @ptrFromInt(used_virt);
    @memset(used_bytes[0..used_size], 0);

    queue.desc = desc_virt;
    queue.avail = avail_virt;
    queue.used = used_virt;
    queue.desc_phys = desc_phys;
    queue.avail_phys = avail_phys;
    queue.used_phys = used_phys;

    const table = descTable(queue);
    for (0..queue_size) |i| table[i].next = @intCast(i + 1);
    table[queue_size - 1].next = NO_FREE_DESC;
    queue.free_head = 0;

    availHeader(queue).idx = 0;
    queue.last_used_idx = 0;

    Regs.QueueDescLow.write(mmio_base, @truncate(desc_phys));
    Regs.QueueDescHigh.write(mmio_base, @truncate(desc_phys >> 32));
    Regs.QueueDriverLow.write(mmio_base, @truncate(avail_phys));
    Regs.QueueDriverHigh.write(mmio_base, @truncate(avail_phys >> 32));
    Regs.QueueDeviceLow.write(mmio_base, @truncate(used_phys));
    Regs.QueueDeviceHigh.write(mmio_base, @truncate(used_phys >> 32));

    Regs.QueueReady.write(mmio_base, 1);
    if (Regs.QueueReady.read(mmio_base) & 1 == 0) return abi.EIO;

    return 0;
}

/// Bounds the completion busy-wait so a misbehaving/unresponsive device
/// fails loudly instead of hanging the boot forever -- the C reference's
/// equivalent loop had no such bound. A real completion lands in a small
/// fraction of this even under QEMU TCG emulation; this is sized to still
/// leave comfortable headroom under the test harness's 30s wall-clock
/// budget if a request never completes at all.
const MAX_POLL_ITERATIONS: u64 = 100_000_000;

/// `VIRTIO_BLK_T_IN` (read: device writes into `data`) -- the one request
/// type whose data descriptor needs `VIRTQ_DESC_F_WRITE`. Baking this one
/// blk-specific constant into an otherwise device-agnostic bus module
/// matches the C reference (`bus_controller.c` special-cased it the same
/// way); nothing here depends on any other blk-specific value.
const VIRTIO_BLK_T_IN: u32 = 0;

fn allocDesc(queue: *abi.VirtioQueue) ?u16 {
    if (queue.free_head == NO_FREE_DESC) return null;
    const idx = queue.free_head;
    queue.free_head = descTable(queue)[idx].next;
    return idx;
}

pub fn submitRequest(
    mmio_base: u64,
    req_type: u32,
    queue: *abi.VirtioQueue,
    req: ?*const anyopaque,
    req_len: u64,
    data: ?*anyopaque,
    data_len: u64,
    status: ?*u8,
) callconv(.c) c_int {
    const table = descTable(queue);
    const has_data = data != null and data_len != 0;

    const desc_header = allocDesc(queue) orelse return abi.ENOMEM;
    const desc_data = if (has_data) allocDesc(queue) orelse return abi.ENOMEM else 0;
    const desc_status = allocDesc(queue) orelse return abi.ENOMEM;

    table[desc_header] = .{
        .addr = phys_mem.virtToPhys(@intFromPtr(req.?)),
        .len = @intCast(req_len),
        .flags = VIRTQ_DESC_F_NEXT,
        .next = if (has_data) desc_data else desc_status,
    };

    if (has_data) {
        table[desc_data] = .{
            .addr = phys_mem.virtToPhys(@intFromPtr(data.?)),
            .len = @intCast(data_len),
            .flags = if (req_type == VIRTIO_BLK_T_IN) VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE else VIRTQ_DESC_F_NEXT,
            .next = desc_status,
        };
    }

    table[desc_status] = .{
        .addr = phys_mem.virtToPhys(@intFromPtr(status.?)),
        .len = 1,
        .flags = VIRTQ_DESC_F_WRITE,
        .next = 0,
    };

    const ring = availRing(queue);
    const avail = availHeader(queue);
    const avail_idx = avail.idx % queue.size;
    ring[avail_idx] = desc_header;

    dmb();
    avail.idx +%= 1;
    dmb();

    Regs.QueueNotify.write(mmio_base, 0);

    const old_used = queue.last_used_idx;
    const used = usedHeader(queue);
    var spins: u64 = 0;
    while (used.idx == old_used) {
        dmb();
        spins += 1;
        if (spins >= MAX_POLL_ITERATIONS) return abi.EIO;
    }

    const used_idx = old_used % queue.size;
    const used_elem = usedRing(queue)[used_idx];

    var cur: u16 = @intCast(used_elem.id);
    while (true) {
        const next = table[cur].next;
        table[cur].next = queue.free_head;
        queue.free_head = cur;
        if (table[cur].flags & VIRTQ_DESC_F_NEXT == 0) break;
        cur = next;
    }

    queue.last_used_idx +%= 1;
    return 0;
}

comptime {
    abi.exportInterface("virtiobus", abi.VirtioBus, .{
        .find_device = findDevice,
        .init_device = initDevice,
        .setup_queue = setupQueue,
        .submit_request = submitRequest,
    });
}

comptime {
    _ = @import("test.zig");
}
