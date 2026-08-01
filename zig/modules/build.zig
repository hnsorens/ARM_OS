const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .aarch64,
        .os_tag = .freestanding,
        .abi = .none,
    });
    const optimize = b.standardOptimizeOption(.{});

    const abi_mod = b.createModule(.{
        .root_source_file = b.path("../shared/abi.zig"),
    });

    var dir = b.build_root.handle.openDir(b.graph.io, ".", .{ .iterate = true }) catch return;
    defer dir.close(b.graph.io);

    var iter = dir.iterate();
    while (iter.next(b.graph.io) catch return) |entry| {
        if (entry.kind != .directory) continue;

        const rel_main_path = b.fmt("{s}/main.zig", .{entry.name});
        _ = b.build_root.handle.statFile(b.graph.io, rel_main_path, .{}) catch continue;

        // 1. Create the module for this specific folder
        const mod = b.createModule(.{
            .root_source_file = b.path(rel_main_path),
            .target = target,
            .optimize = optimize,
        });

        // 2. Direct import link on the module handle FIRST
        mod.addImport("abi", abi_mod);

        // 3. Build executable from the configured module
        const mod_elf = b.addExecutable(.{
            .name = entry.name,
            .root_module = mod,
        });

        // FORCE output filename to have .ko extension
        mod_elf.out_filename = b.fmt("{s}.ko", .{entry.name});

        b.installArtifact(mod_elf);
    }
}
