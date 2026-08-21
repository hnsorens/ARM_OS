//! Tests for the VirtIO-MMIO transport in `main.zig`, split into their own
//! file (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! QEMU's test harness (`zig build qemu-test`) always launches with
//! `-device virtio-blk-device,drive=d0` (see the root `build.zig`), so
//! these exercise a real device at QEMU virt's VirtIO-MMIO window, not a
//! mock -- same approach as `gic_v3/test.zig` against real GICv3 hardware.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");
const main = @import("main.zig");

const serial_if = main.serial_if;
const pmm_if = main.pmm_if;

const VIRTIO_BLK_DEVICE_ID: u32 = 2;
const STATUS_REG_OFFSET: u64 = 0x070;
const STATUS_FEATURES_OK: u32 = 0x08;
const STATUS_DRIVER_OK: u32 = 0x04;

fn testFindDeviceLocatesVirtioBlk() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    return t.result();
}

fn testFindDeviceMissingIdReturnsZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.findDevice(0xFFFF), 0);
    return t.result();
}

fn testInitDeviceHandshakeSucceeds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    t.expectEqual(@src(), main.initDevice(base), 0);

    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    t.expectTrue(@src(), status_reg.* & STATUS_FEATURES_OK != 0);
    return t.result();
}

fn testInitDeviceRejectsZeroBase() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.initDevice(0), abi.EINVAL);
    return t.result();
}

fn testSetupQueueAllocatesRings() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    _ = main.initDevice(base);

    var queue: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);
    t.expectEqual(@src(), queue.size, 8);
    t.expectTrue(@src(), queue.desc != 0);
    t.expectTrue(@src(), queue.avail != 0);
    t.expectTrue(@src(), queue.used != 0);
    t.expectEqual(@src(), queue.free_head, 0);
    return t.result();
}

fn testSubmitRequestReadsBootSector() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.findDevice(VIRTIO_BLK_DEVICE_ID);
    t.expectTrue(@src(), base != 0);
    _ = main.initDevice(base);

    var queue: abi.VirtioQueue = .{ .size = 8 };
    t.expectEqual(@src(), main.setupQueue(base, 0, &queue), 0);

    // Bring the device fully up (DRIVER_OK) -- init_device deliberately
    // stops short of this; a real driver (virtio_blk) does it after its
    // own queue/config setup.
    const status_reg: *volatile u32 = @ptrFromInt(base + STATUS_REG_OFFSET);
    status_reg.* |= STATUS_DRIVER_OK;

    const ReqHeader = extern struct {
        req_type: u32 = 0,
        reserved: u32 = 0,
        sector: u64 = 0,
    };

    var req_virt: u64 = undefined;
    var req_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, @sizeOf(ReqHeader), &req_virt, &req_phys), 0);
    const req: *ReqHeader = @ptrFromInt(req_virt);
    req.* = .{};

    var data_virt: u64 = undefined;
    var data_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, 512, &data_virt, &data_phys), 0);
    const data_ptr: *anyopaque = @ptrFromInt(data_virt);

    var status_virt: u64 = undefined;
    var status_phys: u64 = undefined;
    t.expectEqual(@src(), phys_mem.alloc(pmm_if, 1, &status_virt, &status_phys), 0);
    const status_byte: *u8 = @ptrFromInt(status_virt);
    status_byte.* = 0xFF;

    t.expectEqual(@src(), main.submitRequest(base, 0, &queue, req, @sizeOf(ReqHeader), data_ptr, 512, status_byte), 0);
    t.expectEqual(@src(), status_byte.*, 0);

    return t.result();
}

comptime {
    abi.kernelTest("virtio_bus_find_device_locates_virtio_blk", &testFindDeviceLocatesVirtioBlk);
    abi.kernelTest("virtio_bus_find_device_missing_id_returns_zero", &testFindDeviceMissingIdReturnsZero);
    abi.kernelTest("virtio_bus_init_device_handshake_succeeds", &testInitDeviceHandshakeSucceeds);
    abi.kernelTest("virtio_bus_init_device_rejects_zero_base", &testInitDeviceRejectsZeroBase);
    abi.kernelTest("virtio_bus_setup_queue_allocates_rings", &testSetupQueueAllocatesRings);
    abi.kernelTest("virtio_bus_submit_request_reads_boot_sector", &testSubmitRequestReadsBootSector);
}
