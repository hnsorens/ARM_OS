// modules/dummy/main.zig
const std = @import("std");
const abi = @import("abi");

fn dummyInit() callconv(.c) void {}
fn dummyDeinit() callconv(.c) void {}
fn dummyIoctl(cmd: u32, arg: usize) callconv(.c) i32 {
    _ = cmd;
    _ = arg;
    return 0;
}

// Automatically creates section: .kmodule.export.dummy.test_driver
comptime {
    abi.exportInterface("test_driver", abi.Dummy, .{
        .init = dummyInit,
        .deinit = dummyDeinit,
        .ioctl = dummyIoctl,
    });
}

fn testIoctlReturnsZero() callconv(.c) i32 {
    if (dummyIoctl(1, 0) == 0) return abi.TEST_PASS;
    return abi.TEST_FAIL;
}

comptime {
    abi.kernelTest("ioctl_returns_zero", &testIoctlReturnsZero);
}
