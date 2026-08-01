const std = @import("std");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});

    // 1. Resolve Dependencies
    const boot_dep = b.dependency("bootloader", .{ .optimize = optimize });
    const bootloader_exe = boot_dep.artifact("bootaa64");

    const mods_dep = b.dependency("modules", .{ .optimize = optimize });
    const dummy_ko = mods_dep.artifact("dummy");

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

    // Inject Module via native path resolution
    const mcopy_dummy = b.addSystemCommand(&.{ "mcopy", "-o", "-i", img_name ++ "@@1M" });
    mcopy_dummy.addFileArg(dummy_ko.getEmittedBin());
    mcopy_dummy.addArgs(&.{"::/modules/dummy.ko"});
    mcopy_dummy.step.dependOn(&mcopy_boot.step);

    var last_step: *std.Build.Step = &mcopy_dummy.step;

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
}
