//! Aggregates every bootloader source file with colocated `test {}` blocks
//! so `zig build unit-test` can discover them all from one root. Run
//! natively (not against the `uefi` target) -- each file only exercises
//! the parts of itself that don't call UEFI boot services or emit
//! AArch64-only inline assembly.

test {
    _ = @import("memory/page_table.zig");
    _ = @import("memory/allocator.zig");
    _ = @import("modules/module_loader.zig");
    _ = @import("modules/module_registry.zig");
    _ = @import("uefi/boot_services.zig");
}
