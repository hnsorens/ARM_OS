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
pub const SYS_dup3 = shared.SYS_dup3;
pub const SYS_fcntl = shared.SYS_fcntl;
pub const SYS_ioctl = shared.SYS_ioctl;
pub const SYS_mkdirat = shared.SYS_mkdirat;
pub const SYS_unlinkat = shared.SYS_unlinkat;
pub const SYS_symlinkat = shared.SYS_symlinkat;
pub const SYS_linkat = shared.SYS_linkat;
pub const SYS_renameat = shared.SYS_renameat;
pub const SYS_ftruncate = shared.SYS_ftruncate;
pub const SYS_faccessat = shared.SYS_faccessat;
pub const SYS_chdir = shared.SYS_chdir;
pub const SYS_fchdir = shared.SYS_fchdir;
pub const SYS_fchmodat = shared.SYS_fchmodat;
pub const SYS_fchownat = shared.SYS_fchownat;
pub const SYS_openat = shared.SYS_openat;
pub const SYS_close = shared.SYS_close;
pub const SYS_pipe2 = shared.SYS_pipe2;
pub const SYS_getdents64 = shared.SYS_getdents64;
pub const SYS_lseek = shared.SYS_lseek;
pub const SYS_read = shared.SYS_read;
pub const SYS_write = shared.SYS_write;
pub const SYS_readv = shared.SYS_readv;
pub const SYS_writev = shared.SYS_writev;
pub const SYS_pread64 = shared.SYS_pread64;
pub const SYS_pwrite64 = shared.SYS_pwrite64;
pub const SYS_ppoll = shared.SYS_ppoll;
pub const SYS_readlinkat = shared.SYS_readlinkat;
pub const SYS_newfstatat = shared.SYS_newfstatat;
pub const SYS_fstat = shared.SYS_fstat;
pub const SYS_fsync = shared.SYS_fsync;
pub const SYS_utimensat = shared.SYS_utimensat;
pub const SYS_exit = shared.SYS_exit;
pub const SYS_exit_group = shared.SYS_exit_group;
pub const SYS_set_tid_address = shared.SYS_set_tid_address;
pub const SYS_futex = shared.SYS_futex;
pub const SYS_set_robust_list = shared.SYS_set_robust_list;
pub const SYS_nanosleep = shared.SYS_nanosleep;
pub const SYS_clock_gettime = shared.SYS_clock_gettime;
pub const SYS_clock_getres = shared.SYS_clock_getres;
pub const SYS_clock_nanosleep = shared.SYS_clock_nanosleep;
pub const SYS_sched_yield = shared.SYS_sched_yield;
pub const SYS_sched_getaffinity = shared.SYS_sched_getaffinity;
pub const SYS_kill = shared.SYS_kill;
pub const SYS_tkill = shared.SYS_tkill;
pub const SYS_tgkill = shared.SYS_tgkill;
pub const SYS_rt_sigaction = shared.SYS_rt_sigaction;
pub const SYS_rt_sigprocmask = shared.SYS_rt_sigprocmask;
pub const SYS_setpgid = shared.SYS_setpgid;
pub const SYS_getpgid = shared.SYS_getpgid;
pub const SYS_getsid = shared.SYS_getsid;
pub const SYS_setsid = shared.SYS_setsid;
pub const SYS_uname = shared.SYS_uname;
pub const SYS_umask = shared.SYS_umask;
pub const SYS_prctl = shared.SYS_prctl;
pub const SYS_gettimeofday = shared.SYS_gettimeofday;
pub const SYS_getpid = shared.SYS_getpid;
pub const SYS_getppid = shared.SYS_getppid;
pub const SYS_getuid = shared.SYS_getuid;
pub const SYS_geteuid = shared.SYS_geteuid;
pub const SYS_getgid = shared.SYS_getgid;
pub const SYS_getegid = shared.SYS_getegid;
pub const SYS_gettid = shared.SYS_gettid;
pub const SYS_sysinfo = shared.SYS_sysinfo;
pub const SYS_brk = shared.SYS_brk;
pub const SYS_munmap = shared.SYS_munmap;
pub const SYS_mremap = shared.SYS_mremap;
pub const SYS_clone = shared.SYS_clone;
pub const SYS_execve = shared.SYS_execve;
pub const SYS_mmap = shared.SYS_mmap;
pub const SYS_mprotect = shared.SYS_mprotect;
pub const SYS_madvise = shared.SYS_madvise;
pub const SYS_wait4 = shared.SYS_wait4;
pub const SYS_prlimit64 = shared.SYS_prlimit64;
pub const SYS_getrandom = shared.SYS_getrandom;
pub const SYS_statx = shared.SYS_statx;
pub const SYS_faccessat2 = shared.SYS_faccessat2;
pub const PROT_NONE = shared.PROT_NONE;
pub const PROT_READ = shared.PROT_READ;
pub const PROT_WRITE = shared.PROT_WRITE;
pub const PROT_EXEC = shared.PROT_EXEC;
pub const MAP_PRIVATE = shared.MAP_PRIVATE;
pub const MAP_FIXED = shared.MAP_FIXED;
pub const MAP_ANONYMOUS = shared.MAP_ANONYMOUS;
pub const AT_REMOVEDIR = shared.AT_REMOVEDIR;
pub const AT_SYMLINK_NOFOLLOW = shared.AT_SYMLINK_NOFOLLOW;
pub const AT_EMPTY_PATH = shared.AT_EMPTY_PATH;
pub const SyscallArgs = shared.SyscallArgs;
pub const SyscallHandler = shared.SyscallHandler;
pub const RawSyscallHandler = shared.RawSyscallHandler;
pub const Syscalls = shared.Syscalls;
pub const TrapFrame = shared.TrapFrame;
pub const ExceptionVector = shared.ExceptionVector;
pub const ExceptionOrigin = shared.ExceptionOrigin;
pub const ExceptionOutcome = shared.ExceptionOutcome;
pub const ExceptionCallback = shared.ExceptionCallback;
pub const Exceptions = shared.Exceptions;
pub const Elf = shared.Elf;
pub const KeyListener = shared.KeyListener;
pub const Keyboard = shared.Keyboard;
pub const Tty = shared.Tty;
pub const Fd = shared.Fd;
pub const O_RDONLY = shared.O_RDONLY;
pub const O_WRONLY = shared.O_WRONLY;
pub const O_RDWR = shared.O_RDWR;
pub const O_CREAT = shared.O_CREAT;
pub const O_TRUNC = shared.O_TRUNC;
pub const O_APPEND = shared.O_APPEND;
pub const O_DIRECTORY = shared.O_DIRECTORY;
pub const AT_FDCWD = shared.AT_FDCWD;
pub const SEEK_SET = shared.SEEK_SET;
pub const SEEK_CUR = shared.SEEK_CUR;
pub const SEEK_END = shared.SEEK_END;
pub const ECHILD = shared.ECHILD;
pub const ESPIPE = shared.ESPIPE;
pub const EMFILE = shared.EMFILE;
pub const ENFILE = shared.ENFILE;
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
pub const KStat = shared.KStat;
pub const UtsName = shared.UtsName;
pub const DT_UNKNOWN = shared.DT_UNKNOWN;
pub const DT_FIFO = shared.DT_FIFO;
pub const DT_CHR = shared.DT_CHR;
pub const DT_DIR = shared.DT_DIR;
pub const DT_BLK = shared.DT_BLK;
pub const DT_REG = shared.DT_REG;
pub const DT_LNK = shared.DT_LNK;
pub const DT_SOCK = shared.DT_SOCK;
pub const F_DUPFD = shared.F_DUPFD;
pub const F_GETFD = shared.F_GETFD;
pub const F_SETFD = shared.F_SETFD;
pub const F_GETFL = shared.F_GETFL;
pub const F_SETFL = shared.F_SETFL;
pub const F_DUPFD_CLOEXEC = shared.F_DUPFD_CLOEXEC;
pub const FD_CLOEXEC = shared.FD_CLOEXEC;
pub const TCGETS = shared.TCGETS;
pub const TCSETS = shared.TCSETS;
pub const TCSETSW = shared.TCSETSW;
pub const TCSETSF = shared.TCSETSF;
pub const TIOCGWINSZ = shared.TIOCGWINSZ;
pub const TIOCSWINSZ = shared.TIOCSWINSZ;
pub const TIOCGPGRP = shared.TIOCGPGRP;
pub const TIOCSPGRP = shared.TIOCSPGRP;
pub const FIONREAD = shared.FIONREAD;

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
pub const ENOEXEC = shared.ENOEXEC;
pub const EBADF = shared.EBADF;
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
pub const EPERM = shared.EPERM;
pub const EINTR = shared.EINTR;
pub const ENXIO = shared.ENXIO;
pub const E2BIG = shared.E2BIG;
pub const EAGAIN = shared.EAGAIN;
pub const EACCES = shared.EACCES;
pub const ENODEV = shared.ENODEV;
pub const ENOTTY = shared.ENOTTY;
pub const ERANGE = shared.ERANGE;
pub const EPIPE = shared.EPIPE;
pub const EDESTADDRREQ = shared.EDESTADDRREQ;

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
