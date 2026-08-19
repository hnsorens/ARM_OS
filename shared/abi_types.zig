//! Shared ABI types used by both the bootloader and kernel modules.
//!
//! This file must contain NO side-effectful exports (no `_start`, no link
//! sections), so the bootloader can import it freely. Module-only entry/export
//! machinery lives in `abi.zig`, which re-exports everything from here.

pub const PAGE_SIZE: u64 = 4096;

/// Virtual base of the region where modules are loaded (upper half, TTBR1).
pub const VIRTUAL_MODULE_LOAD_START: u64 = 0xFFFFFF8000000000;

/// Offset applied to a physical address to reach its HHDM direct-map alias.
pub const HHDM_OFFSET: u64 = 0xFFFF800000000000;

/// Boot stack size in pages, mapped at VIRTUAL_MODULE_LOAD_START.
pub const MODULE_STACK_SIZE_PAGES: u64 = 0x100;

/// The first module is loaded this far past VIRTUAL_MODULE_LOAD_START so it
/// does not collide with the boot stack (the C bootloader had this bug).
pub const INITIAL_LOAD_OFFSET: u64 = MODULE_STACK_SIZE_PAGES * PAGE_SIZE;

// --- Test result codes (must match the C TEST_ macros) ---
pub const TEST_PASS: i32 = 0;
pub const TEST_FAIL: i32 = 1;
pub const TEST_SKIP: i32 = 2;

// --- errno values used by module vtables (match the kernel errno.h) ---
pub const EINVAL: c_int = 22;
pub const ENOMEM: c_int = 12;
pub const EFAULT: c_int = 14;
pub const EBUSY: c_int = 16;
pub const EEXIST: c_int = 17;
pub const EOVERFLOW: c_int = 139;
pub const EIO: c_int = 5;

// --- Boot info passed to every module entry ---
pub const MemoryType = enum(u32) {
    unknown = 0,
    free = 1,
    used = 2,
};

pub const MemoryRegion = extern struct {
    start: u64,
    /// Number of 4 KiB pages in this region.
    page_count: u64,
    memory_type: MemoryType,
};

pub const BootInfo = extern struct {
    memory_regions: [*]MemoryRegion,
    memory_map_size: u64,
    virtual_start: u64,
};

// --- Vtable types. Signatures match the C include/api/*.h headers. ---

// NOTE: the C reference driver exposes this as a variadic `printf(fmt, ...)`.
// This toolchain's C-varargs support (`@cVaStart`) is disabled ("disabled
// due to miscompilations" in std.builtin), so a real variadic function body
// can't be written at all here. Every caller in this codebase is a Zig
// module anyway, so there's no ABI reason to keep C varargs: modules format
// with `std.fmt` at the call site (see `shared/kernel_fmt.zig`) and this
// interface only needs to move already-formatted bytes.
pub const Serial = extern struct {
    write: *const fn (ptr: [*]const u8, len: usize) callconv(.c) usize,
};

pub const Pmm = extern struct {
    alloc_page: *const fn (page_order: u8, out_frame: *u64) callconv(.c) c_int,
    alloc_aligned: *const fn (count: u64, alignment: u64, out: *u64) callconv(.c) c_int,
    alloc_in_range: *const fn (count: u64, max_addr: u64, out: *u64) callconv(.c) c_int,
    retain: *const fn (frame: u64) callconv(.c) c_int,
    release: *const fn (frame: u64) callconv(.c) c_int,
    get_total_memory: *const fn () callconv(.c) u64,
    get_free_memory: *const fn () callconv(.c) u64,
    reserve_range: *const fn (start: u64, sz: u64) callconv(.c) c_int,
};

pub const PageSize = enum(u32) {
    ps_4kb = 0x1000,
    ps_2mb = 0x200000,
    ps_1gb = 0x40000000,
};

// MMU flag bitmask constants (architectural Stage 1 descriptor fields).
pub const MMU_RO: u64 = 1 << 7;
pub const MMU_USER: u64 = 1 << 6;
pub const MMU_NO_EXEC: u64 = 1 << 54;
pub const MMU_NOCACHE: u64 = 1 << 2;
pub const MMU_WRITE_THROUGH: u64 = 2 << 2;

pub const Mmu = extern struct {
    alloc: *const fn (out_root: *u64) callconv(.c) c_int,
    free: *const fn (root: u64) callconv(.c) c_int,
    copy: *const fn (src_root: u64, dest_root: *u64) callconv(.c) c_int,
    set_user_ctx: *const fn (root: u64, asid: u16) callconv(.c) c_int,
    set_kernel_ctx: *const fn (root: u64, asid: u16) callconv(.c) c_int,
    get_user_ctx: *const fn (root: *u64) callconv(.c) c_int,
    get_kernel_ctx: *const fn (root: *u64) callconv(.c) c_int,
    map: *const fn (root: u64, virt: u64, phys: u64, pg_count: u64, pg_size: PageSize, flags: u64) callconv(.c) c_int,
    unmap: *const fn (root: u64, virt: u64, pg_count: u64, pg_size: PageSize) callconv(.c) c_int,
    protect: *const fn (root: u64, virt: u64, pg_count: u64, pg_size: PageSize, flags: u64) callconv(.c) c_int,
    translate: *const fn (root: u64, virt: u64, phys_out: *u64, flags_out: *u64) callconv(.c) c_int,
    flush: *const fn () callconv(.c) c_int,
    invalidate: *const fn (virt: u64, pg_count: u64, pg_size: PageSize) callconv(.c) c_int,
    set_mair: *const fn (mair: u64) callconv(.c) c_int,
};

pub const RegionType = enum(u32) {
    free = 0,
    code = 1,
    data = 2,
    stack = 3,
    heap = 4,
    mmio = 5,
    guard = 6,
};

pub const VmmRegionInfo = extern struct {
    base: u64,
    size: u64,
    flags: u64,
    region_type: RegionType,
    is_paged: bool,
};

pub const Vmm = extern struct {
    space_create: *const fn (out_table_root: *u64) callconv(.c) c_int,
    space_destroy: *const fn (table_root: u64) callconv(.c) c_int,
    allocate: *const fn (root: u64, vaddr: *u64, sz: u64, flags: u64, region_type: RegionType) callconv(.c) c_int,
    reserve: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
    free: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
    resize: *const fn (root: u64, vaddr: u64, old_sz: u64, new_sz: u64) callconv(.c) c_int,
    map_external: *const fn (root: u64, v: u64, p: u64, sz: u64, f: u64) callconv(.c) c_int,
    protect: *const fn (root: u64, vaddr: u64, sz: u64, new_flags: u64) callconv(.c) c_int,
    query: *const fn (root: u64, vaddr: u64, out_info: *VmmRegionInfo) callconv(.c) c_int,
    activate: *const fn (root: u64) callconv(.c) c_int,
    sync: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
};

pub const Heap = extern struct {
    create: *const fn (root: u64, sz: u64, out_heap: *?*anyopaque) callconv(.c) c_int,
    destroy: *const fn (heap: ?*anyopaque) callconv(.c) c_int,
    malloc: *const fn (heap: ?*anyopaque, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    free: *const fn (heap: ?*anyopaque, ptr: ?*anyopaque) callconv(.c) c_int,
    realloc: *const fn (heap: ?*anyopaque, ptr: ?*anyopaque, new_size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    memalign: *const fn (heap: ?*anyopaque, alignment: u64, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    get_stats: *const fn (heap: ?*anyopaque, used: *u64, total: *u64) callconv(.c) c_int,
};

pub const Slab = extern struct {
    create_cache: *const fn (root: u64, obj_size: u64, alignment: u64, out_cache: *?*anyopaque) callconv(.c) c_int,
    destroy_cache: *const fn (cache: ?*anyopaque) callconv(.c) c_int,
    alloc: *const fn (cache: ?*anyopaque, out_obj: *?*anyopaque) callconv(.c) c_int,
    free: *const fn (cache: ?*anyopaque, obj: ?*anyopaque) callconv(.c) c_int,
    shrink: *const fn (cache: ?*anyopaque) callconv(.c) c_int,
};

pub const IrqTrigger = enum(u32) {
    level = 0,
    edge = 1,
};

pub const IrqGroup = enum(u32) {
    secure = 0,
    non_secure = 1,
};

pub const IsrHandler = *const fn (context: ?*anyopaque) callconv(.c) void;

pub const InterruptManager = extern struct {
    init_global: *const fn (d_base: u64, r_base: u64) callconv(.c) c_int,
    init_core: *const fn () callconv(.c) c_int,
    set_core_priority_mask: *const fn (mask: u32) callconv(.c) c_int,
    enable: *const fn (vector: u32) callconv(.c) c_int,
    disable: *const fn (vector: u32) callconv(.c) c_int,
    configure: *const fn (vector: u32, trigger: IrqTrigger, priority: u32) callconv(.c) c_int,
    set_group: *const fn (vector: u32, group: IrqGroup) callconv(.c) c_int,
    route_to_core: *const fn (vector: u32, mpidr_or_apicid: u64) callconv(.c) c_int,
    acknowledge: *const fn () callconv(.c) u32,
    end_of_interrupt: *const fn (vector: u32) callconv(.c) c_int,
    register_handler: *const fn (vector: u32, handler: IsrHandler, arg: ?*anyopaque) callconv(.c) c_int,
    unregister_handler: *const fn (vector: u32) callconv(.c) c_int,
};

pub const TimerCallback = *const fn (id: u32, context: ?*anyopaque) callconv(.c) void;

pub const Timer = extern struct {
    register_callback: *const fn (ticks_period: u32, periodic: u8, callback: TimerCallback, id: *u32, context: ?*anyopaque) callconv(.c) c_int,
    unregister_callback: *const fn (id: u32) callconv(.c) c_int,
    pause: *const fn (id: u32) callconv(.c) c_int,
    unpause: *const fn (id: u32) callconv(.c) c_int,
    modify: *const fn (id: u32, new_period: u32) callconv(.c) c_int,
    get_system_ticks: *const fn (ticks: *u64) callconv(.c) c_int,
    delay_ticks: *const fn (ticks: u32) callconv(.c) c_int,
};

// --- Module metadata linking ---

/// One entry in the `.kmodule.tests` section: a named test function.
/// A test returns TEST_PASS / TEST_FAIL / TEST_SKIP.
pub const TestEntry = extern struct {
    name: [*:0]const u8,
    func: *const fn () callconv(.c) i32,
};

pub const EXPORT_SECTION_PREFIX = ".kmodule.export.";
pub const IMPORT_SECTION_PREFIX = ".kmodule.import.";
pub const CONFIG_SECTION_PREFIX = ".kmodule.config.";
pub const TESTS_SECTION = ".kmodule.tests";

pub fn exportSection(comptime type_name: []const u8, comptime instance_name: []const u8) []const u8 {
    return EXPORT_SECTION_PREFIX ++ type_name ++ "." ++ instance_name;
}

/// A module's import placeholder names only the interface *type* it needs
/// (e.g. "vmm"), never a specific provider instance -- which module
/// actually fills it in is decided by the [Dependencies] wiring in
/// kernel.ini, not baked into the module's own code.
pub fn importSection(comptime type_name: []const u8) []const u8 {
    return IMPORT_SECTION_PREFIX ++ type_name;
}

/// A module's config placeholder names the parse `kind` ("int"/"bool"/
/// "str" -- how the bootloader should interpret the kernel.ini text value)
/// and the `key` (the name kernel.ini's `[module.config]` section uses).
/// The section's byte size (recovered from the ELF itself, not from this
/// name) tells the bootloader exactly how many bytes to overwrite.
pub fn configSection(comptime kind: []const u8, comptime key: []const u8) []const u8 {
    return CONFIG_SECTION_PREFIX ++ kind ++ "." ++ key;
}
