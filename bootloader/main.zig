const std = @import("std");
const uefi = std.os.uefi;
const shared = @import("shared_types");

const Serial = @import("logging/serial.zig");
const Filesystem = @import("uefi/filesystem.zig");
const BootServices = @import("uefi/boot_services.zig");
const Allocation = @import("memory/allocator.zig");
const PageTableMod = @import("memory/page_table.zig");
const PageTable = PageTableMod.PageTable;
const Registry = @import("modules/module_registry.zig");
const ImportMod = @import("modules/module_import.zig");
const ModuleLoaderMod = @import("modules/module_loader.zig");
const DependencyGraphMod = @import("modules/dependency_graph.zig");
const ConfigMod = @import("modules/module_config.zig");

/// Globals instead of locals because both outlive the boot-time stack
/// switch in `main()` -- `beginKernel` (running on the new stack) still
/// needs to reach them. This mirrors the C bootloader's global `BootInfo`.
var g_registry: Registry.DriverRegistry = undefined;
var g_boot_info: shared.BootInfo = undefined;
var g_bump: Allocation.BumpAllocator = undefined;
var g_graph: DependencyGraphMod.DependencyGraph = undefined;
var g_config_values: ConfigMod.ConfigValues = undefined;

fn haltForever() noreturn {
    while (true) {
        asm volatile ("wfi");
    }
}

fn kernelPanic() noreturn {
    asm volatile ("msr daifset, #2");
    Serial.failLog("!!! KERNEL PANIC !!!");
    Serial.failLog("System Halted");
    haltForever();
}

/// Runs on the freshly switched-to boot stack: initializes every registered
/// module in dependency order, runs their tests, and reports the summary
/// line the QEMU test harness watches for.
fn beginKernel() callconv(.c) noreturn {
    Serial.okLog("Beginning initialization");

    const tally = g_registry.initializeAll(@ptrCast(&g_boot_info)) catch {
        Serial.failLog("Initializing modules");
        kernelPanic();
    };
    Serial.okLog("Initializing modules");
    Serial.okLog("Boot successful");
    Serial.logSummary(tally.passed, tally.failed, tally.skipped);

    startInit();
    haltForever();
}

/// Loads `/init` via the elf_loader module's registered vtable, admits it
/// to the scheduler, and runs the scheduler idle loop -- this is where
/// "kernel" hands off to "userspace", after every module (and the test
/// suite) has finished. Returns (falling through to haltForever) only if
/// the loader/scheduler aren't present or `/init` fails to load.
fn startInit() void {
    const elf_di = g_registry.get("elf", "loader") orelse {
        Serial.failLog("Locating /init loader");
        return;
    };
    const sched_di = g_registry.get("scheduler", "roundrobin") orelse {
        Serial.failLog("Locating scheduler");
        return;
    };
    const elf: *const shared.Elf = @ptrCast(@alignCast(elf_di.vtable_ptr));
    const sched: *const shared.Scheduler = @ptrCast(@alignCast(sched_di.vtable_ptr));

    var pid: u32 = 0;
    if (elf.load("/init", &pid) != 0) {
        Serial.failLog("Loading /init");
        return;
    }
    if (sched.admit(pid) != 0) {
        Serial.failLog("Admitting /init");
        return;
    }

    // Put the console in canonical + echo mode for the interactive shell
    // (the tty module's own tests leave it in whatever state they last set).
    if (g_registry.get("tty", "console")) |tty_di| {
        const tty: *const shared.Tty = @ptrCast(@alignCast(tty_di.vtable_ptr));
        tty.set_mode(true, true);
    }
    Serial.okLog("Starting /init");

    // Console input arrives as a UART IRQ; unmask it at EL1 so this idle
    // loop actually services it (and wakes a process blocked in read()).
    asm volatile ("msr daifclr, #2" ::: .{ .memory = true });
    while (true) {
        _ = sched.run();
        asm volatile ("msr daifclr, #2" ::: .{ .memory = true });
        asm volatile ("wfi");
    }
}

pub fn main() void {
    Serial.bootLogStart();

    const root = Filesystem.openRoot() catch return;
    Serial.okLog("Opening Root");

    const bs = uefi.system_table.boot_services.?;

    // Registry/import bookkeeping needs to keep allocating *after*
    // ExitBootServices (dependency edges get recorded while resolving
    // imports, which has to happen post-MMU-enable). Boot services are
    // gone by then, so back it with a plain bump allocator over a block
    // reserved now, instead of an allocator that calls back into UEFI.
    const bump_block = bs.allocatePool(.loader_data, 4 * 1024 * 1024) catch {
        Serial.failLog("Allocated registry arena");
        return;
    };
    Serial.okLog("Allocated registry arena");
    g_bump = .{ .buffer = bump_block };
    const allocator = g_bump.allocator();

    g_registry = Registry.DriverRegistry.init(allocator);
    var imports = ImportMod.ModuleImportHandles.init(allocator, 32) catch {
        Serial.failLog("Initializing module import handles");
        return;
    };
    Serial.okLog("Initializing module import handles");

    g_graph = DependencyGraphMod.DependencyGraph.init(allocator);
    g_config_values = ConfigMod.ConfigValues.init(allocator);
    var configs = ConfigMod.ModuleConfigHandles.init(allocator, 32) catch {
        Serial.failLog("Initializing module config handles");
        return;
    };
    Serial.okLog("Initializing module config handles");

    // Upper half (TTBR1): modules and the boot stack get mapped explicitly
    // as they're loaded/allocated below; nothing is pre-populated here.
    var upper_table = PageTable{};

    var loader = ModuleLoaderMod.ModuleLoader.init(shared.INITIAL_LOAD_OFFSET);
    const config_path = std.unicode.utf8ToUtf16LeStringLiteral("\\kernel.ini");
    loader.loadKernel(allocator, root, config_path, &upper_table, &g_registry, &imports, &g_graph, &configs, &g_config_values) catch {
        Serial.failLog("Loading kernel");
        return;
    };
    Serial.okLog("Loading kernel");

    const stack_pages = bs.allocatePages(.any, .runtime_services_code, shared.MODULE_STACK_SIZE_PAGES) catch {
        Serial.failLog("Allocating stack");
        return;
    };
    Serial.okLog("Allocating stack");

    upper_table.mapMemory(
        shared.VIRTUAL_MODULE_LOAD_START,
        @intFromPtr(stack_pages.ptr),
        .four_kb,
        shared.MODULE_STACK_SIZE_PAGES,
    ) catch {
        Serial.failLog("Mapping stack memory");
        return;
    };
    Serial.okLog("Mapping stack memory");

    // Learn the true extent of physical memory *before* exiting boot
    // services: QEMU's virt board, given enough RAM, splits it into a low
    // window and a high window (e.g. firmware code/allocations have been
    // observed landing around 18GB with `-m 16G`), so nothing below can be
    // assumed to fit under any fixed size.
    var map_prep = BootServices.prepareMemoryMap() catch return;

    // Lower half (TTBR0): flat identity map covering everything up to the
    // top of physical memory, so the bootloader's own code/data/stack, its
    // freshly allocated page tables, and MMIO (e.g. the UART) all stay
    // reachable at their current addresses once the MMU is on.
    const identity_map_size = std.mem.alignForward(u64, map_prep.max_physical_addr, 1024 * 1024 * 1024);
    var lower_table = PageTable.createIdentity(identity_map_size) catch {
        Serial.failLog("Creating lower identity page table");
        return;
    };
    Serial.okLog("Creating lower identity page table");

    // Upper half (TTBR1) also gets the same range mapped again at
    // HHDM_OFFSET (virt = phys + HHDM_OFFSET): the higher-half direct map
    // kernel modules use (e.g. the pmm's free-list nodes, threaded through
    // free physical pages via their HHDM alias) needs this to exist ahead
    // of time, not just whatever addresses modules/the stack happened to
    // get explicitly mapped at.
    const one_gb: u64 = 1024 * 1024 * 1024;
    const hhdm_blocks = identity_map_size / one_gb;
    upper_table.mapMemory(shared.HHDM_OFFSET, 0, .one_gb, hhdm_blocks) catch {
        Serial.failLog("Mapping HHDM");
        return;
    };
    Serial.okLog("Mapping HHDM");

    const kernel_map = BootServices.finishExitAndBuildMap(uefi.handle, &map_prep) catch return;

    // No boot services (and no UEFI console) past this point; our own UART
    // driver only ever poked MMIO directly, so serial logging still works.
    PageTableMod.enableTranslation(&lower_table, &upper_table);
    Serial.okLog("Enabling page table");

    // Do this after the MMU is enabled: import placeholders and exported
    // vtables live at virtual addresses that only became valid just now.
    imports.resolveAll(&g_registry, &g_graph) catch {
        Serial.failLog("Handling module imports");
        return;
    };

    // Same reasoning as above: config placeholders live at virtual
    // addresses that only became valid just now.
    configs.resolveAll(&g_config_values);
    Serial.okLog("Applied module config values");

    g_boot_info = .{
        .memory_regions = kernel_map.regions.ptr,
        .memory_map_size = kernel_map.regions.len,
        .virtual_start = shared.VIRTUAL_MODULE_LOAD_START + loader.load_offset,
    };

    Serial.okLog("Setting stack pointer");
    const new_sp = shared.VIRTUAL_MODULE_LOAD_START + shared.MODULE_STACK_SIZE_PAGES * shared.PAGE_SIZE;
    asm volatile (
        \\ mov sp, %[new_sp]
        \\ mov x29, #0
        :
        : [new_sp] "r" (new_sp),
        : .{ .memory = true }
    );

    beginKernel();
}
