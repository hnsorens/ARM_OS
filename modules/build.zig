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

    const kernel_fmt_mod = b.createModule(.{
        .root_source_file = b.path("../shared/kernel_fmt.zig"),
    });
    kernel_fmt_mod.addImport("abi", abi_mod);

    const kernel_test_mod = b.createModule(.{
        .root_source_file = b.path("../shared/kernel_test.zig"),
    });
    kernel_test_mod.addImport("abi", abi_mod);
    kernel_test_mod.addImport("kernel_fmt", kernel_fmt_mod);

    const mmio_mod = b.createModule(.{
        .root_source_file = b.path("../shared/mmio.zig"),
    });

    const sysreg_mod = b.createModule(.{
        .root_source_file = b.path("../shared/sysreg.zig"),
    });

    const spinlock_mod = b.createModule(.{
        .root_source_file = b.path("../shared/spinlock.zig"),
    });

    var dir = b.build_root.handle.openDir(b.graph.io, ".", .{ .iterate = true }) catch return;
    defer dir.close(b.graph.io);

    // Modules live at arbitrary depth (e.g. "hnsorens/memory/pmm/main.zig",
    // matching the namespaced layout the C modules used), so this has to
    // walk recursively rather than just iterate the top level.
    var walker = dir.walk(b.allocator) catch return;
    defer walker.deinit();

    while (walker.next(b.graph.io) catch return) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.eql(u8, entry.basename, "main.zig")) continue;

        // entry.path looks like "hnsorens/memory/pmm/main.zig"; the module
        // name is its containing directory path with '/' replaced by '.',
        // matching kernel.ini's dotted module names (e.g.
        // "hnsorens.memory.pmm = Y"). A top-level module like
        // "dummy/main.zig" has no '/' left to replace, giving just "dummy".
        const rel_path = b.dupe(entry.path);
        const name = b.dupe(entry.path[0 .. entry.path.len - "/main.zig".len]);
        std.mem.replaceScalar(u8, name, '/', '.');

        const mod = b.createModule(.{
            .root_source_file = b.path(rel_path),
            .target = target,
            .optimize = optimize,
        });

        // Direct import link on the module handle FIRST.
        mod.addImport("abi", abi_mod);
        mod.addImport("kernel_fmt", kernel_fmt_mod);
        mod.addImport("kernel_test", kernel_test_mod);
        mod.addImport("mmio", mmio_mod);
        mod.addImport("sysreg", sysreg_mod);
        mod.addImport("spinlock", spinlock_mod);

        // Build executable from the configured module. PIE so the linker
        // emits R_AARCH64_RELATIVE relocations instead of baking in
        // absolute addresses everywhere -- the bootloader loads each
        // module at a runtime-chosen address and needs those relocations
        // to fix up pointers (e.g. exported vtable function pointers).
        const mod_elf = b.addExecutable(.{
            .name = name,
            .root_module = mod,
        });
        mod_elf.pie = true;

        // FORCE output filename to have .ko extension
        mod_elf.out_filename = b.fmt("{s}.ko", .{name});

        b.installArtifact(mod_elf);
    }
}
