//! Per-module config values from kernel.ini's `[module_name.config]`
//! sections, and the ELF-side placeholders (`.kmodule.config.<kind>.<key>`,
//! declared via `abi.declareConfig*`) they get written into.
//!
//! Mirrors the dependency_graph.zig / module_import.zig split: `ConfigValues`
//! is data recovered from kernel.ini text (parsed in module_loader.zig),
//! `ModuleConfigHandles` is data recovered from a loaded module's ELF
//! sections (populated in module_loader.zig's loadElf), and `resolveAll`
//! bridges the two -- deferred until after the MMU is enabled, since a
//! config placeholder's section address is a virtual address in the
//! not-yet-active upper-half table (same reason module_import.zig's
//! resolveAll is deferred).
//!
//! kernel.ini syntax:
//!
//!     [hnsorens.io.serial_debug]
//!     enable = true
//!     [hnsorens.io.serial_debug.config]
//!     baud_rate = 115200
//!
//! writes "115200" into whatever `abi.declareConfigInt("baud_rate", ...)`
//! placeholder that module declared. A key kernel.ini doesn't mention is
//! left at its compiled-in default -- `resolveAll` only touches handles it
//! finds a matching value for.

const std = @import("std");
const Serial = @import("../logging/serial.zig");

const ConfigEntry = struct {
    module_name: []const u8,
    key: []const u8,
    value: []const u8,
};

pub const ConfigValues = struct {
    allocator: std.mem.Allocator,
    /// Linear list rather than a keyed map: entry counts here are tens, not
    /// thousands (same rationale as `DependencyGraph.edges`), so a linear
    /// scan is free and this sidesteps growing a hash map against the
    /// boot-time bump allocator.
    entries: std.ArrayList(ConfigEntry) = .empty,

    pub fn init(allocator: std.mem.Allocator) ConfigValues {
        return .{ .allocator = allocator };
    }

    pub fn deinit(self: *ConfigValues) void {
        self.entries.deinit(self.allocator);
    }

    pub fn add(self: *ConfigValues, module_name: []const u8, key: []const u8, value: []const u8) !void {
        try self.entries.append(self.allocator, .{ .module_name = module_name, .key = key, .value = value });
    }

    pub fn get(self: *const ConfigValues, module_name: []const u8, key: []const u8) ?[]const u8 {
        for (self.entries.items) |e| {
            if (std.mem.eql(u8, e.module_name, module_name) and std.mem.eql(u8, e.key, key)) return e.value;
        }
        return null;
    }
};

const ConfigHandle = struct {
    module_name: []const u8,
    /// "int", "bool", or "str" -- how to parse the kernel.ini text value.
    /// An unrecognized kind is skipped (forward-compat), not a hard error.
    kind: []const u8,
    key: []const u8,
    vtable_ptr: *anyopaque,
    size: u64,
};

pub const ModuleConfigHandles = struct {
    allocator: std.mem.Allocator,
    handles: std.ArrayList(ConfigHandle) = .empty,

    pub fn init(allocator: std.mem.Allocator, init_capacity: usize) !ModuleConfigHandles {
        var handles: std.ArrayList(ConfigHandle) = .empty;
        try handles.ensureTotalCapacity(allocator, init_capacity);
        return .{ .allocator = allocator, .handles = handles };
    }

    pub fn deinit(self: *ModuleConfigHandles) void {
        self.handles.deinit(self.allocator);
    }

    pub fn add(self: *ModuleConfigHandles, handle: ConfigHandle) !void {
        try self.handles.append(self.allocator, handle);
    }

    fn writeBool(dst: [*]u8, text: []const u8) void {
        const truthy = text.len > 0 and (text[0] == 't' or text[0] == 'T' or text[0] == '1');
        dst[0] = if (truthy) 1 else 0;
    }

    fn writeInt(dst: [*]u8, size: u64, text: []const u8) void {
        const parsed = std.fmt.parseInt(i64, text, 0) catch {
            Serial.failLog("Parsed int config value");
            return;
        };
        const bytes: [8]u8 = @bitCast(parsed);
        const n: usize = @intCast(@min(size, bytes.len));
        @memcpy(dst[0..n], bytes[0..n]);
    }

    fn writeStr(dst: [*]u8, size: u64, text: []const u8) void {
        const n: usize = @intCast(size);
        @memset(dst[0..n], 0);
        const copy_len = @min(n, text.len);
        @memcpy(dst[0..copy_len], text[0..copy_len]);
    }

    /// Writes each queued handle's kernel.ini value (if any) into place.
    /// Must only be called once the MMU is enabled -- see the module
    /// doc-comment.
    pub fn resolveAll(self: *ModuleConfigHandles, values: *const ConfigValues) void {
        for (self.handles.items) |handle| {
            const text = values.get(handle.module_name, handle.key) orelse continue;
            const dst: [*]u8 = @ptrCast(handle.vtable_ptr);

            if (std.mem.eql(u8, handle.kind, "bool")) {
                writeBool(dst, text);
            } else if (std.mem.eql(u8, handle.kind, "int")) {
                writeInt(dst, handle.size, text);
            } else if (std.mem.eql(u8, handle.kind, "str")) {
                writeStr(dst, handle.size, text);
            } else {
                continue;
            }
            Serial.okLog("Applied module config value");
        }
    }
};

const testing = std.testing;

test "ConfigValues add/get round-trips by (module, key), null when absent" {
    var values = ConfigValues.init(testing.allocator);
    defer values.deinit();

    try values.add("hnsorens.io.serial_debug", "baud_rate", "115200");
    try testing.expectEqualStrings("115200", values.get("hnsorens.io.serial_debug", "baud_rate").?);
    try testing.expect(values.get("hnsorens.io.serial_debug", "other_key") == null);
    try testing.expect(values.get("other.module", "baud_rate") == null);
}

test "ModuleConfigHandles.resolveAll writes bool/int/str, skips missing keys and unknown kinds" {
    var values = ConfigValues.init(testing.allocator);
    defer values.deinit();
    try values.add("m", "flag", "true");
    try values.add("m", "count", "42");
    try values.add("m", "name", "hi");

    var handles = try ModuleConfigHandles.init(testing.allocator, 4);
    defer handles.deinit();

    var bool_storage: u8 = 0xFF;
    var int_storage: u32 = 0xFFFFFFFF;
    var str_storage: [4]u8 = .{ 'X', 'X', 'X', 'X' };
    var untouched: u8 = 0xAB;
    var unknown_kind: u8 = 0xCD;

    try handles.add(.{ .module_name = "m", .kind = "bool", .key = "flag", .vtable_ptr = &bool_storage, .size = 1 });
    try handles.add(.{ .module_name = "m", .kind = "int", .key = "count", .vtable_ptr = &int_storage, .size = 4 });
    try handles.add(.{ .module_name = "m", .kind = "str", .key = "name", .vtable_ptr = &str_storage, .size = 4 });
    try handles.add(.{ .module_name = "m", .kind = "bool", .key = "no_such_key", .vtable_ptr = &untouched, .size = 1 });
    try handles.add(.{ .module_name = "m", .kind = "weird", .key = "flag", .vtable_ptr = &unknown_kind, .size = 1 });

    handles.resolveAll(&values);

    try testing.expectEqual(@as(u8, 1), bool_storage);
    try testing.expectEqual(@as(u32, 42), int_storage);
    try testing.expectEqualStrings("hi\x00\x00", &str_storage);
    try testing.expectEqual(@as(u8, 0xAB), untouched);
    try testing.expectEqual(@as(u8, 0xCD), unknown_kind);
}
