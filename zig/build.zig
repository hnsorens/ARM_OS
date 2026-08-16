const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});

    // 1. Resolve Dependencies
    const boot_dep = b.dependency("bootloader", .{ .optimize = optimize });
    const bootloader_exe = boot_dep.artifact("bootaa64");

    const mods_dep = b.dependency("modules", .{ .optimize = optimize });

    // 2. Disk Image Setup Commands (Direct Executables, No Shell)
    const img_name = "disk.img";

    const truncate_cmd = b.addSystemCommand(&.{ "truncate", "-s", "128M", img_name });

    const sgdisk_init = b.addSystemCommand(&.{ "sgdisk", "-o", img_name });
    sgdisk_init.step.dependOn(&truncate_cmd.step);

    const sgdisk_part = b.addSystemCommand(&.{ "sgdisk", "-n", "1:2048:262110", "-t", "1:ef00", img_name });
    sgdisk_part.step.dependOn(&sgdisk_init.step);

    const mformat_cmd = b.addSystemCommand(&.{ "mformat", "-i", img_name ++ "@@1M", "-F", "-H", "2048", "-c", "1", "-v", "ESP", "::" });
    mformat_cmd.step.dependOn(&sgdisk_part.step);

    // Create Directory Hierarchy inside FAT image
    const mmd_efi = b.addSystemCommand(&.{ "mmd", "-i", img_name ++ "@@1M", "::/EFI" });
    mmd_efi.step.dependOn(&mformat_cmd.step);

    const mmd_boot = b.addSystemCommand(&.{ "mmd", "-i", img_name ++ "@@1M", "::/EFI/BOOT" });
    mmd_boot.step.dependOn(&mmd_efi.step);

    const mmd_mods = b.addSystemCommand(&.{ "mmd", "-i", img_name ++ "@@1M", "::/modules" });
    mmd_mods.step.dependOn(&mmd_boot.step);

    // Inject Bootloader via native path resolution
    const mcopy_boot = b.addSystemCommand(&.{ "mcopy", "-i", img_name ++ "@@1M" });
    mcopy_boot.addFileArg(bootloader_exe.getEmittedBin());
    mcopy_boot.addArgs(&.{"::/EFI/BOOT/BOOTAA64.EFI"});
    mcopy_boot.step.dependOn(&mmd_mods.step);

    // Inject every discovered module (any "modules/**/main.zig" file, at
    // arbitrary depth) via native path resolution. Names are dotted paths
    // matching kernel.ini (e.g. "hnsorens.memory.pmm"), same derivation as
    // modules/build.zig uses to name the executables in the first place.
    var last_step: *std.Build.Step = &mcopy_boot.step;

    var modules_dir = b.build_root.handle.openDir(b.graph.io, "modules", .{ .iterate = true }) catch @panic("open modules dir");
    defer modules_dir.close(b.graph.io);

    var modules_walker = modules_dir.walk(b.allocator) catch @panic("walk modules dir");
    defer modules_walker.deinit();

    while (modules_walker.next(b.graph.io) catch @panic("walk modules dir")) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.eql(u8, entry.basename, "main.zig")) continue;

        const name = b.dupe(entry.path[0 .. entry.path.len - "/main.zig".len]);
        std.mem.replaceScalar(u8, name, '/', '.');

        const module_artifact = mods_dep.artifact(name);
        const mcopy_module = b.addSystemCommand(&.{ "mcopy", "-o", "-i", img_name ++ "@@1M" });
        mcopy_module.addFileArg(module_artifact.getEmittedBin());
        mcopy_module.addArgs(&.{b.fmt("::/modules/{s}.ko", .{name})});
        mcopy_module.step.dependOn(last_step);
        last_step = &mcopy_module.step;
    }

    // Check if kernel.ini exists using native Zig filesystem calls (No 'if [ -f ]' shell check)
    const kernel_ini_exists = if (b.build_root.handle.statFile(b.graph.io, "kernel.ini", .{})) |_| true else |_| false;

    if (kernel_ini_exists) {
        const mcopy_ini = b.addSystemCommand(&.{
            "mcopy", "-o", "-i", img_name ++ "@@1M", "kernel.ini", "::/kernel.ini",
        });
        mcopy_ini.step.dependOn(last_step);
        last_step = &mcopy_ini.step;
    }

    const image_step = b.step("image", "Assemble raw disk.img with bootloader and modules");
    image_step.dependOn(last_step);

    // 3. QEMU Target
    const run_cmd = b.addSystemCommand(&.{ "qemu-system-aarch64" });
    run_cmd.addArgs(&.{
        "-m", "16G",
        "-cpu", "cortex-a72",
        "-smp", "4",
        "-M", "virt,gic-version=3",
        "-accel", "tcg,thread=multi",
        "-bios", "/usr/share/edk2/aarch64/QEMU_EFI.fd",
        "-drive", "file=disk.img,format=raw,if=none,id=d0",
        "-device", "virtio-blk-device,drive=d0",
        "-mem-prealloc",
        "-gdb", "tcp::1234",
        "-nographic",
    });

    run_cmd.step.dependOn(image_step);

    const run_step = b.step("run", "Compile all binaries, provision disk.img, and execute QEMU");
    run_step.dependOn(&run_cmd.step);

    // 4. QEMU-based module test harness.
    //
    // The bootloader always halts in a WFI loop once it's done (there's no
    // real kernel to hand off to yet), so it never exits on its own --
    // `timeout` bounds the run, and `|| true` keeps that expected non-zero
    // exit from failing this step. Whether the run actually passed is
    // decided by the checker tool below, which inspects what got printed
    // to serial before the timeout hit.
    const qemu_log_path = "qemu-test-output.log";
    const qemu_test_script = b.fmt(
        "timeout 30 qemu-system-aarch64 -m 16G -cpu cortex-a72 -smp 4 -M virt,gic-version=3 " ++
            "-accel tcg,thread=multi -bios /usr/share/edk2/aarch64/QEMU_EFI.fd " ++
            "-drive file={s},format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0 " ++
            "-mem-prealloc -nographic -serial mon:stdio -display none > {s} 2>&1 || true",
        .{ img_name, qemu_log_path },
    );
    const run_qemu_test = b.addSystemCommand(&.{ "bash", "-c", qemu_test_script });
    run_qemu_test.step.dependOn(image_step);

    const checker_exe = b.addExecutable(.{
        .name = "qemu_test_check",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/qemu_test_check.zig"),
            .target = b.graph.host,
        }),
    });

    const run_checker = b.addRunArtifact(checker_exe);
    run_checker.addArg(qemu_log_path);
    run_checker.step.dependOn(&run_qemu_test.step);

    const qemu_test_step = b.step("qemu-test", "Boot the image in QEMU and check module test results from serial output");
    qemu_test_step.dependOn(&run_checker.step);

    // 5. Native unit tests for bootloader logic that doesn't touch UEFI
    // boot services or emit AArch64-only inline assembly (ELF relocation
    // math, page table bit-packing, memory map coalescing, the registry's
    // bookkeeping, the post-ExitBootServices bump allocator).
    const shared_types_native_mod = b.createModule(.{
        .root_source_file = b.path("shared/abi_types.zig"),
    });

    const unit_test_mod = b.createModule(.{
        .root_source_file = b.path("bootloader/unit_tests.zig"),
        .target = b.graph.host,
    });
    unit_test_mod.addImport("shared_types", shared_types_native_mod);

    const unit_tests = b.addTest(.{ .root_module = unit_test_mod });
    const run_unit_tests = b.addRunArtifact(unit_tests);

    const unit_test_step = b.step("unit-test", "Run native unit tests for bootloader logic");
    unit_test_step.dependOn(&run_unit_tests.step);

    const test_step = b.step("test", "Run unit tests and the QEMU integration test");
    test_step.dependOn(&run_unit_tests.step);
    test_step.dependOn(&run_checker.step);
}
