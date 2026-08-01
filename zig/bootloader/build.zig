const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .aarch64,
        .os_tag = .uefi,
        .abi = .msvc,
    });
    const optimize = b.standardOptimizeOption(.{});

    const bootloader = b.addExecutable(.{
        .name = "bootaa64",
        .root_module = b.createModule(.{
            .root_source_file = b.path("main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    b.installArtifact(bootloader);
}
