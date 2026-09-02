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
pub const TaskContext = shared.TaskContext;
pub const ContextSwitch = shared.ContextSwitch;
pub const ProcessState = shared.ProcessState;
pub const ProcessInfo = shared.ProcessInfo;
pub const Process = shared.Process;
pub const Scheduler = shared.Scheduler;
pub const SYSCALL_TABLE_SIZE = shared.SYSCALL_TABLE_SIZE;
pub const SYS_getcwd = shared.SYS_getcwd;
pub const SYS_dup = shared.SYS_dup;
pub const SYS_chdir = shared.SYS_chdir;
pub const SYS_openat = shared.SYS_openat;
pub const SYS_close = shared.SYS_close;
pub const SYS_getdents64 = shared.SYS_getdents64;
pub const SYS_lseek = shared.SYS_lseek;
pub const SYS_read = shared.SYS_read;
pub const SYS_write = shared.SYS_write;
pub const SYS_fstat = shared.SYS_fstat;
pub const SYS_exit = shared.SYS_exit;
pub const SYS_exit_group = shared.SYS_exit_group;
pub const SYS_sched_yield = shared.SYS_sched_yield;
pub const SYS_getpid = shared.SYS_getpid;
pub const SYS_getppid = shared.SYS_getppid;
pub const SYS_brk = shared.SYS_brk;
pub const SYS_clone = shared.SYS_clone;
pub const SYS_execve = shared.SYS_execve;
pub const SYS_wait4 = shared.SYS_wait4;
pub const SyscallArgs = shared.SyscallArgs;
pub const SyscallHandler = shared.SyscallHandler;
pub const Syscalls = shared.Syscalls;
pub const TrapFrame = shared.TrapFrame;
pub const ExceptionVector = shared.ExceptionVector;
pub const ExceptionOrigin = shared.ExceptionOrigin;
pub const ExceptionOutcome = shared.ExceptionOutcome;
pub const ExceptionCallback = shared.ExceptionCallback;
pub const Exceptions = shared.Exceptions;
pub const TestEntry = shared.TestEntry;

pub const VirtioQueue = shared.VirtioQueue;
pub const VirtioBus = shared.VirtioBus;
pub const BlkDevice = shared.BlkDevice;
pub const GptPartition = shared.GptPartition;
pub const Gpt = shared.Gpt;
pub const Ext2DirEntry = shared.Ext2DirEntry;
pub const Ext2Stat = shared.Ext2Stat;
pub const Ext2 = shared.Ext2;
pub const Vfs = shared.Vfs;

pub const EXT2_FT_UNKNOWN = shared.EXT2_FT_UNKNOWN;
pub const EXT2_FT_REG_FILE = shared.EXT2_FT_REG_FILE;
pub const EXT2_FT_DIR = shared.EXT2_FT_DIR;
pub const EXT2_FT_CHRDEV = shared.EXT2_FT_CHRDEV;
pub const EXT2_FT_BLKDEV = shared.EXT2_FT_BLKDEV;
pub const EXT2_FT_FIFO = shared.EXT2_FT_FIFO;
pub const EXT2_FT_SOCK = shared.EXT2_FT_SOCK;
pub const EXT2_FT_SYMLINK = shared.EXT2_FT_SYMLINK;
pub const EXT2_ROOT_INO = shared.EXT2_ROOT_INO;
pub const EXT2_NAME_LEN = shared.EXT2_NAME_LEN;

pub const EXT2_S_IFSOCK = shared.EXT2_S_IFSOCK;
pub const EXT2_S_IFLNK = shared.EXT2_S_IFLNK;
pub const EXT2_S_IFREG = shared.EXT2_S_IFREG;
pub const EXT2_S_IFBLK = shared.EXT2_S_IFBLK;
pub const EXT2_S_IFDIR = shared.EXT2_S_IFDIR;
pub const EXT2_S_IFCHR = shared.EXT2_S_IFCHR;
pub const EXT2_S_IFIFO = shared.EXT2_S_IFIFO;
pub const EXT2_S_IFMT = shared.EXT2_S_IFMT;

pub const TEST_PASS = shared.TEST_PASS;
pub const TEST_FAIL = shared.TEST_FAIL;
pub const TEST_SKIP = shared.TEST_SKIP;

pub const HHDM_OFFSET = shared.HHDM_OFFSET;

pub const EINVAL = shared.EINVAL;
pub const ENOSYS = shared.ENOSYS;
pub const ENOMEM = shared.ENOMEM;
pub const EFAULT = shared.EFAULT;
pub const EBUSY = shared.EBUSY;
pub const EEXIST = shared.EEXIST;
pub const EOVERFLOW = shared.EOVERFLOW;
pub const EIO = shared.EIO;
pub const ENOENT = shared.ENOENT;
pub const ENOTDIR = shared.ENOTDIR;
pub const EISDIR = shared.EISDIR;
pub const ENOSPC = shared.ENOSPC;
pub const ENOTEMPTY = shared.ENOTEMPTY;
pub const ELOOP = shared.ELOOP;
pub const ENAMETOOLONG = shared.ENAMETOOLONG;

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

/// Declares a placeholder for InterfaceType, to be filled in with a
/// provider's exported vtable before this module's `_start`/entry function
/// runs. The module only says *what type* it needs -- which module
/// actually provides it is resolved by the bootloader from the
/// `[Dependencies]` wiring in kernel.ini (falling back to "the sole
/// registered provider of this type" if that module has no explicit entry
/// there). The returned pointer must not be dereferenced before entry.
pub fn importInterface(comptime InterfaceType: type) *const InterfaceType {
    return comptime blk: {
        const type_prefix = getTypeNameLower(InterfaceType);
        const section_name = shared.importSection(type_prefix);
        const holder = struct {
            var storage: InterfaceType align(8) linksection(section_name) = undefined;
        };
        @export(&holder.storage, .{ .name = "__kmodule_import_" ++ section_name });
        break :blk &holder.storage;
    };
}

/// Declares a placeholder for a config value of kind `kind` ("int", "bool",
/// or "str"), initialized to `default`. Placed in section
/// `.kmodule.config.<kind>.<key>`; the bootloader overwrites it in place
/// (after the MMU is enabled -- see `importInterface`) with whatever value
/// `kernel.ini`'s `[module.config]` section names for `key`, if any. The
/// returned pointer must not be dereferenced before this module's
/// `_start`/entry function runs, same rule as `importInterface`.
fn declareConfig(comptime kind: []const u8, comptime key: []const u8, comptime T: type, default: T) *const T {
    return comptime blk: {
        const section_name = shared.configSection(kind, key);
        const holder = struct {
            var storage: T align(8) linksection(section_name) = default;
        };
        @export(&holder.storage, .{ .name = "__kmodule_config_" ++ section_name });
        break :blk &holder.storage;
    };
}

/// Declares an integer config value. `T` may be any sized integer type
/// (e.g. `u32`, `i16`) -- the bootloader writes the low `@sizeOf(T)` bytes
/// of the parsed value, so `T`'s width is what bounds the accepted range.
pub fn declareConfigInt(comptime key: []const u8, comptime T: type, default: T) *const T {
    return declareConfig("int", key, T, default);
}

/// Declares a boolean config value.
pub fn declareConfigBool(comptime key: []const u8, default: bool) *const bool {
    return declareConfig("bool", key, bool, default);
}

/// Declares a fixed-capacity, NUL-terminated string config value. `default`
/// must fit within `max_len` bytes (not counting the terminator).
pub fn declareConfigStr(comptime key: []const u8, comptime max_len: usize, comptime default: []const u8) *const [max_len:0]u8 {
    if (default.len > max_len) @compileError("declareConfigStr: default string longer than max_len");
    comptime var buf: [max_len:0]u8 = [_:0]u8{0} ** max_len;
    comptime @memcpy(buf[0..default.len], default);
    return declareConfig("str", key, [max_len:0]u8, buf);
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
