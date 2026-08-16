const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .aarch64,
        .os_tag = .uefi,
        .abi = .msvc,
    });
    const optimize = b.standardOptimizeOption(.{});

    const shared_types_mod = b.createModule(.{
        .root_source_file = b.path("../shared/abi_types.zig"),
    });

    const bootloader_mod = b.createModule(.{
        .root_source_file = b.path("main.zig"),
        .target = target,
        .optimize = optimize,
    });
    bootloader_mod.addImport("shared_types", shared_types_mod);

    const bootloader = b.addExecutable(.{
        .name = "bootaa64",
        .root_module = bootloader_mod,
    });

    b.installArtifact(bootloader);
}
