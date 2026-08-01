const std = @import("std");
const root = @import("root");

// --- Default _start stub ---

fn defaultStart() callconv(.c) noreturn {
    if (@hasDecl(root, "main")) {
        _ = root.main();
    }
    while (true) {}
}

comptime {
    // If the module doesn't define its own _start, export defaultStart weakly
    if (!@hasDecl(root, "_start")) {
        @export(&defaultStart, .{
            .name = "_start",
            .linkage = .weak,
        });
    }
}

pub const Dummy = extern struct {
    init: *const fn () callconv(.c) void,
    deinit: *const fn () callconv(.c) void,
    ioctl: *const fn (cmd: u32, arg: usize) callconv(.c) i32,
};

pub const Serial = extern struct {
    write: *const fn (buf: [*]const u8, len: usize) callconv(.c) usize,
};

// --- Generic Module Descriptor ---

pub fn ModuleExport(comptime VTable: type) type {
    return extern struct {
        version: u32 = 1,
        vtable: VTable,
    };
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

/// Automatically uses @typeName(InterfaceType) for the category name.
/// Example: exportInterface("test_driver", abi.Dummy, ...) 
/// -> linksection(".kmodule.export.dummy.test_driver")
pub fn exportInterface(
    comptime instance_name: []const u8,
    comptime InterfaceType: type,
    vtable_impl: InterfaceType,
) void {
    comptime {
        const type_prefix = getTypeNameLower(InterfaceType);
        const section_name = ".kmodule.export." ++ type_prefix ++ "." ++ instance_name;
        const ExportType = ModuleExport(InterfaceType);

        _ = struct {
            export var descriptor align(8) linksection(section_name) = ExportType{
                .version = 1,
                .vtable = vtable_impl,
            };
        };
    }
}
