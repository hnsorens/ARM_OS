//! Module-side ABI helpers: entry point plumbing, and export/import/test
//! section machinery built on top of the plain data types in `abi_types.zig`.
//!
//! Only kernel modules should import this file (it has side-effectful
//! exports). The bootloader imports `abi_types.zig` directly instead.

const std = @import("std");
const root = @import("root");
const shared = @import("abi_types.zig");

// Re-export the plain ABI types so modules can write `abi.Serial`, `abi.Pmm`,
// etc. without also importing abi_types.zig directly.
pub const BootInfo = shared.BootInfo;
pub const MemoryRegion = shared.MemoryRegion;
pub const MemoryType = shared.MemoryType;
pub const Serial = shared.Serial;
pub const Pmm = shared.Pmm;
pub const Mmu = shared.Mmu;
pub const PageSize = shared.PageSize;
pub const Vmm = shared.Vmm;
pub const VmmRegionInfo = shared.VmmRegionInfo;
pub const RegionType = shared.RegionType;
pub const Heap = shared.Heap;
pub const Slab = shared.Slab;
pub const InterruptManager = shared.InterruptManager;
pub const IrqTrigger = shared.IrqTrigger;
pub const IrqGroup = shared.IrqGroup;
pub const IsrHandler = shared.IsrHandler;
pub const Timer = shared.Timer;
pub const TimerCallback = shared.TimerCallback;
pub const TestEntry = shared.TestEntry;

pub const TEST_PASS = shared.TEST_PASS;
pub const TEST_FAIL = shared.TEST_FAIL;
pub const TEST_SKIP = shared.TEST_SKIP;

pub const HHDM_OFFSET = shared.HHDM_OFFSET;

pub const EINVAL = shared.EINVAL;
pub const ENOMEM = shared.ENOMEM;
pub const EFAULT = shared.EFAULT;
pub const EBUSY = shared.EBUSY;
pub const EEXIST = shared.EEXIST;
pub const EOVERFLOW = shared.EOVERFLOW;
pub const EIO = shared.EIO;

pub const MMU_RO = shared.MMU_RO;
pub const MMU_USER = shared.MMU_USER;
pub const MMU_NO_EXEC = shared.MMU_NO_EXEC;
pub const MMU_NOCACHE = shared.MMU_NOCACHE;
pub const MMU_WRITE_THROUGH = shared.MMU_WRITE_THROUGH;

/// Interface type used by the `dummy` test module (io.dummy).
pub const Dummy = extern struct {
    init: *const fn () callconv(.c) void,
    deinit: *const fn () callconv(.c) void,
    ioctl: *const fn (cmd: u32, arg: usize) callconv(.c) i32,
};

// --- Default module entry point ---
//
// The bootloader's module registry calls a module's ELF entry point
// (`e_entry`) directly as `fn (*anyopaque) callconv(.c) void`, once, with a
// pointer to the boot info struct, after that module's dependencies have
// been initialized. The function is expected to return (it is a one-shot
// init routine, not a program that runs forever).

fn defaultStart(boot_info: *anyopaque) callconv(.c) void {
    if (@hasDecl(root, "main")) {
        root.main(boot_info);
    }
}

comptime {
    // If the module doesn't define its own _start, export defaultStart weakly.
    if (!@hasDecl(root, "_start")) {
        @export(&defaultStart, .{
            .name = "_start",
            .linkage = .weak,
        });
    }
}

/// Helper function to convert a type name (e.g. "abi.Dummy" or "Dummy")
/// into a lowercased slice at comptime (e.g. "dummy").
fn getTypeNameLower(comptime T: type) []const u8 {
    const full_name = @typeName(T);

    // Extract everything after the last dot (handles "abi.Dummy" -> "Dummy")
    const short_name = if (std.mem.lastIndexOfScalar(u8, full_name, '.')) |idx|
        full_name[idx + 1 ..]
    else
        full_name;

    // Convert to lowercase at comptime
    var buffer: [short_name.len]u8 = undefined;
    _ = std.ascii.lowerString(&buffer, short_name);
    const final_name = buffer;

    return &final_name;
}

/// Registers `vtable_impl` as this module's export of `InterfaceType` under
/// `instance_name`. Placed in section `.kmodule.export.<type>.<instance>`,
/// picked up by the bootloader's ELF loader while scanning section headers.
pub fn exportInterface(
    comptime instance_name: []const u8,
    comptime InterfaceType: type,
    vtable_impl: InterfaceType,
) void {
    comptime {
        const type_prefix = getTypeNameLower(InterfaceType);
        const section_name = shared.exportSection(type_prefix, instance_name);

        // `export`'s symbol name is never auto-mangled per instantiation
        // (that would defeat the point of a stable external name), so a
        // module exporting more than one interface needs an explicit
        // unique name per call rather than the field identifier itself.
        const holder = struct {
            var descriptor: InterfaceType align(8) linksection(section_name) = vtable_impl;
        };
        @export(&holder.descriptor, .{ .name = "__kmodule_export_" ++ section_name });
    }
}

/// Declares a placeholder for InterfaceType, to be filled in with a specific
/// instance's exported vtable (chosen by the bootloader if `instance_name`
/// resolves ambiguously via `importInterfaceAny`). The returned pointer must
/// not be dereferenced until after the module's `_start`/entry function
/// runs -- the bootloader fills it in before calling any module entry point.
pub fn importInterface(
    comptime instance_name: []const u8,
    comptime InterfaceType: type,
) *const InterfaceType {
    return comptime blk: {
        const type_prefix = getTypeNameLower(InterfaceType);
        const section_name = shared.importSection(type_prefix, instance_name);
        const holder = struct {
            var storage: InterfaceType align(8) linksection(section_name) = undefined;
        };
        @export(&holder.storage, .{ .name = "__kmodule_import_" ++ section_name });
        break :blk &holder.storage;
    };
}

/// Same as `importInterface`, but lets the bootloader pick whichever
/// instance of `InterfaceType` is registered (there must be exactly one
/// candidate, or the bootloader import resolution fails).
pub fn importInterfaceAny(comptime InterfaceType: type) *const InterfaceType {
    return comptime blk: {
        const type_prefix = getTypeNameLower(InterfaceType);
        const section_name = shared.importSectionAny(type_prefix);
        const holder = struct {
            var storage: InterfaceType align(8) linksection(section_name) = undefined;
        };
        @export(&holder.storage, .{ .name = "__kmodule_import_" ++ section_name });
        break :blk &holder.storage;
    };
}

/// Registers a named test function, run by the bootloader after this
/// module's `_start`/entry function completes. `func` must return
/// `TEST_PASS`, `TEST_FAIL`, or `TEST_SKIP`.
pub fn kernelTest(comptime name: [:0]const u8, comptime func: *const fn () callconv(.c) i32) void {
    comptime {
        // The exported symbol name must be unique per call (a module can,
        // and typically does, register several tests) -- naming every one
        // "entry" would collide the moment there's more than one.
        const holder = struct {
            var entry: shared.TestEntry align(8) linksection(shared.TESTS_SECTION) = .{
                .name = name.ptr,
                .func = func,
            };
        };
        @export(&holder.entry, .{ .name = "__kernel_test_" ++ name });
    }
}
