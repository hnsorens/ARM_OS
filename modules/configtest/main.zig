// modules/configtest/main.zig
//
// Exercises the bootloader's kernel.ini -> module config pipeline: this
// module declares an int, a bool, and a string config value with
// deliberately "wrong" compiled-in defaults, and its test asserts the
// runtime values match the *non-default* values kernel.ini sets for it
// (see [configtest.config] there) -- which only happens if the bootloader
// actually parsed that section and wrote the values into these
// placeholders before this module's entry point ran.
const std = @import("std");
const abi = @import("abi");

const retry_count = abi.declareConfigInt("retry_count", u32, 3);
const enabled_feature = abi.declareConfigBool("enabled_feature", false);
const label = abi.declareConfigStr("label", 16, "default");

fn testConfigValuesMatchKernelIni() callconv(.c) i32 {
    if (retry_count.* != 42) return abi.TEST_FAIL;
    if (enabled_feature.* != true) return abi.TEST_FAIL;
    if (!std.mem.eql(u8, std.mem.sliceTo(label, 0), "hello")) return abi.TEST_FAIL;
    return abi.TEST_PASS;
}

comptime {
    abi.kernelTest("config_values_match_kernel_ini", &testConfigValuesMatchKernelIni);
}
