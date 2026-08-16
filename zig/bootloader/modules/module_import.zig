//! Pending import placeholders discovered while scanning module ELF
//! sections, resolved once (after the MMU is enabled, since both the
//! placeholder and the exporter's vtable live at virtual addresses that are
//! only valid once mapped).

const std = @import("std");
const Registry = @import("module_registry.zig");
const Serial = @import("../logging/serial.zig");

pub const ImportHandle = struct {
    type_name: []const u8,
    /// Null means "any" -- the bootloader picks the sole registered
    /// instance of `type_name`.
    inst_name: ?[]const u8,
    parent_type: []const u8,
    parent_name: []const u8,
    vtable_ptr: *anyopaque,
};

pub const ModuleImportHandles = struct {
    allocator: std.mem.Allocator,
    handles: std.ArrayList(ImportHandle) = .empty,

    pub fn init(allocator: std.mem.Allocator, init_capacity: usize) !ModuleImportHandles {
        var handles: std.ArrayList(ImportHandle) = .empty;
        try handles.ensureTotalCapacity(allocator, init_capacity);
        return .{ .allocator = allocator, .handles = handles };
    }

    pub fn add(self: *ModuleImportHandles, handle: ImportHandle) !void {
        try self.handles.append(self.allocator, handle);
    }

    /// Resolves each pending import against the registry: registers the
    /// dependency edge (so init order accounts for it) and copies the
    /// exporter's vtable bytes into the importer's placeholder storage.
    pub fn resolveAll(self: *ModuleImportHandles, registry: *Registry.DriverRegistry) !void {
        for (self.handles.items) |handle| {
            const inst_name = handle.inst_name orelse registry.resolveAnyName(handle.type_name) orelse {
                Serial.failLog("Resolved import (no provider registered)");
                return error.NoProvider;
            };

            registry.addDependency(handle.parent_type, handle.parent_name, handle.type_name, inst_name) catch |err| {
                Serial.failLog("Put dependency in registry");
                return err;
            };
            Serial.okLog("Put dependency in registry");

            const exporter = registry.get(handle.type_name, inst_name) orelse {
                Serial.failLog("Resolved import (export vanished)");
                return error.ExportNotFound;
            };

            const dst: [*]u8 = @ptrCast(handle.vtable_ptr);
            const src: [*]const u8 = @ptrCast(exporter.vtable_ptr);
            @memcpy(dst[0..exporter.vtable_size], src[0..exporter.vtable_size]);
        }
        Serial.okLog("Handled module imports");
    }
};
