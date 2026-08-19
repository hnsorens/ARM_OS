// modules/dummy2/main.zig
//
// Exercises the bootloader's import-resolution path: this module imports
// the `Dummy` interface exported by the `dummy` module (by category only,
// letting the bootloader resolve the single registered instance) and calls
// through it from a test, so the loader's dependency graph, vtable copy,
// and init ordering are all exercised end to end.
const abi = @import("abi");

const dummy_if = abi.importInterface(abi.Dummy);

fn testCallsImportedDummy() callconv(.c) i32 {
    dummy_if.init();
    if (dummy_if.ioctl(7, 0) == 0) return abi.TEST_PASS;
    return abi.TEST_FAIL;
}

comptime {
    abi.kernelTest("calls_imported_dummy_ioctl", &testCallsImportedDummy);
}
