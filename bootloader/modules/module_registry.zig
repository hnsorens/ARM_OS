//! Tracks every exported module interface (by "type.instance", e.g.
//! "dummy.test_driver") and, once dependencies between them have been
//! recorded (see module_import.zig), initializes each module exactly once
//! in dependency order and runs its tests.
//!
//! Mirrors the C bootloader's module_registry.c, but instances are
//! addressed by category+name in a hash map instead of a fixed-capacity
//! linear-probed table, and dependencies are (type, name) pairs resolved on
//! demand instead of raw pointers (simpler, and avoids any risk of pointer
//! invalidation across map growth).

const std = @import("std");
const shared = @import("shared_types");

const Serial = @import("../logging/serial.zig");

pub const Dependency = struct {
    type_name: []const u8,
    inst_name: []const u8,
};

pub const DriverInfo = struct {
    driver_type: []const u8,
    driver_name: []const u8,
    vtable_ptr: *anyopaque,
    vtable_size: u64,
    /// Module's ELF entry point, called once as `fn(boot_info)` after all
    /// dependencies have initialized. Null for entry-less "test only"
    /// modules (matches the C convention: e_entry == 0).
    entry_fn: ?*const fn (*anyopaque) callconv(.c) void,
    tests: []const shared.TestEntry,
    dependencies: std.ArrayList(Dependency) = .empty,
    initialized: bool = false,
};

pub const DriverSubMap = std.StringArrayHashMapUnmanaged(DriverInfo);
pub const RegistryMap = std.StringArrayHashMapUnmanaged(DriverSubMap);

const OrderEntry = struct {
    type_name: []const u8,
    inst_name: []const u8,
};

const ModuleOwnerEntry = struct {
    module_name: []const u8,
    owner: Dependency,
};

pub const TestTally = struct {
    passed: u32 = 0,
    failed: u32 = 0,
    skipped: u32 = 0,
};

pub const DriverRegistry = struct {
    map: RegistryMap = .{},
    allocator: std.mem.Allocator,
    /// Insertion order, so initialization has a deterministic starting scan
    /// order (the actual order modules run in is dependency order, per
    /// `initOne`'s recursion).
    order: std.ArrayList(OrderEntry) = .empty,
    /// kernel.ini module name -> the (type, instance) it registered as its
    /// own export identity (see `module_loader.zig`'s owner_type/owner_name).
    /// Lets the `[Dependencies]` graph name a provider by its kernel.ini
    /// name (stable, human-authored) instead of its internal export
    /// instance name.
    module_owner: std.ArrayList(ModuleOwnerEntry) = .empty,

    pub fn init(allocator: std.mem.Allocator) DriverRegistry {
        return .{ .allocator = allocator };
    }

    pub fn deinit(self: *DriverRegistry) void {
        var cat_it = self.map.iterator();
        while (cat_it.next()) |cat_entry| {
            var inst_it = cat_entry.value_ptr.iterator();
            while (inst_it.next()) |inst_entry| {
                inst_entry.value_ptr.dependencies.deinit(self.allocator);
            }
            cat_entry.value_ptr.deinit(self.allocator);
        }
        self.map.deinit(self.allocator);
        self.order.deinit(self.allocator);
        self.module_owner.deinit(self.allocator);
    }

    /// Records that kernel.ini module `module_name` owns the (type_name,
    /// inst_name) export identity, so `[Dependencies]` edges naming
    /// `module_name` as a provider can be translated to the right registry
    /// entry. Called once per loaded module, regardless of whether it
    /// exports a real interface (see `MODULE_CATEGORY_FALLBACK`).
    pub fn registerModuleOwner(self: *DriverRegistry, module_name: []const u8, type_name: []const u8, inst_name: []const u8) !void {
        try self.module_owner.append(self.allocator, .{ .module_name = module_name, .owner = .{ .type_name = type_name, .inst_name = inst_name } });
    }

    pub fn resolveModuleOwner(self: *DriverRegistry, module_name: []const u8) ?Dependency {
        for (self.module_owner.items) |entry| {
            if (std.mem.eql(u8, entry.module_name, module_name)) return entry.owner;
        }
        return null;
    }

    pub fn put(self: *DriverRegistry, info: DriverInfo) !void {
        const cat_gop = try self.map.getOrPut(self.allocator, info.driver_type);
        if (!cat_gop.found_existing) cat_gop.value_ptr.* = .{};

        const inst_gop = try cat_gop.value_ptr.getOrPut(self.allocator, info.driver_name);
        if (inst_gop.found_existing) return error.DuplicateModule;
        inst_gop.value_ptr.* = info;

        try self.order.append(self.allocator, .{ .type_name = info.driver_type, .inst_name = info.driver_name });
    }

    pub fn get(self: *DriverRegistry, type_name: []const u8, inst_name: []const u8) ?*DriverInfo {
        const sub = self.map.getPtr(type_name) orelse return null;
        return sub.getPtr(inst_name);
    }

    /// Resolves an "import any" request: succeeds only if exactly the
    /// category is registered (ambiguity/absence are both errors upstream).
    pub fn resolveAnyName(self: *DriverRegistry, type_name: []const u8) ?[]const u8 {
        const sub = self.map.get(type_name) orelse return null;
        if (sub.count() == 0) return null;
        return sub.keys()[0];
    }

    pub fn addDependency(self: *DriverRegistry, type_name: []const u8, inst_name: []const u8, dep_type: []const u8, dep_name: []const u8) !void {
        const entry = self.get(type_name, inst_name) orelse return error.InstanceNotFound;
        try entry.dependencies.append(self.allocator, .{ .type_name = dep_type, .inst_name = dep_name });
    }

    const MAX_DEPTH = 10;

    fn initOne(self: *DriverRegistry, type_name: []const u8, inst_name: []const u8, boot_info: *anyopaque, tally: *TestTally, depth: u32) !void {
        if (depth > MAX_DEPTH) return error.DependencyCycle;

        // Re-fetch on every call instead of holding a pointer across the
        // recursive calls below: no `put` happens during initialization, so
        // this is purely a defensive-clarity choice, not a correctness one.
        const entry = self.get(type_name, inst_name) orelse return error.InstanceNotFound;
        if (entry.initialized) return;

        if (entry.entry_fn == null) {
            entry.initialized = true;
            return;
        }

        for (entry.dependencies.items) |dep| {
            try self.initOne(dep.type_name, dep.inst_name, boot_info, tally, depth + 1);
        }

        const e = self.get(type_name, inst_name).?;
        if (!e.initialized) {
            e.entry_fn.?(boot_info);
            runTests(e.driver_name, e.tests, tally);
            e.initialized = true;
        }
    }

    fn runTests(inst_name: []const u8, tests: []const shared.TestEntry, tally: *TestTally) void {
        for (tests) |t| {
            const name = std.mem.sliceTo(t.name, 0);
            const result = t.func();
            if (result == shared.TEST_PASS) {
                Serial.testOkLog(inst_name, name);
                tally.passed += 1;
            } else if (result == shared.TEST_SKIP) {
                Serial.testSkipLog(inst_name, name);
                tally.skipped += 1;
            } else {
                Serial.testFailLog(inst_name, name);
                tally.failed += 1;
            }
        }
    }

    /// Initializes every registered module in dependency order, then runs
    /// the tests of any entry-less ("test only") modules, which are skipped
    /// by the first pass. Matches the C bootloader's two-pass
    /// InitializeModules.
    pub fn initializeAll(self: *DriverRegistry, boot_info: *anyopaque) !TestTally {
        var tally = TestTally{};

        for (self.order.items) |o| {
            try self.initOne(o.type_name, o.inst_name, boot_info, &tally, 0);
        }

        for (self.order.items) |o| {
            const e = self.get(o.type_name, o.inst_name).?;
            if (e.entry_fn == null) {
                runTests(e.driver_name, e.tests, &tally);
            }
        }

        return tally;
    }
};

// --- Unit tests -------------------------------------------------------
//
// `initializeAll`/`initOne`/`runTests` aren't covered here: they call
// through to `Serial`, which pokes UART MMIO directly and would fault on
// the native test target. That path is exercised by the QEMU integration
// test instead (which asserts on the resulting [SUMMARY] line). Everything
// tested here is the registry bookkeeping itself: registration, lookup,
// and dependency-edge recording.

const testing = std.testing;

fn testInfo(driver_type: []const u8, driver_name: []const u8) DriverInfo {
    return .{
        .driver_type = driver_type,
        .driver_name = driver_name,
        .vtable_ptr = @ptrFromInt(0x1000),
        .vtable_size = 8,
        .entry_fn = null,
        .tests = &.{},
    };
}

test "put/get round-trip an instance by (type, name)" {
    var reg = DriverRegistry.init(testing.allocator);
    defer reg.deinit();

    try reg.put(testInfo("dummy", "a"));

    const found = reg.get("dummy", "a") orelse return error.TestUnexpectedResult;
    try testing.expectEqualStrings("dummy", found.driver_type);
    try testing.expectEqualStrings("a", found.driver_name);
    try testing.expect(reg.get("dummy", "b") == null);
    try testing.expect(reg.get("other", "a") == null);
}

test "put rejects a duplicate (type, name)" {
    var reg = DriverRegistry.init(testing.allocator);
    defer reg.deinit();

    try reg.put(testInfo("dummy", "a"));
    try testing.expectError(error.DuplicateModule, reg.put(testInfo("dummy", "a")));
}

test "resolveAnyName finds the sole instance of a category, null if none registered" {
    var reg = DriverRegistry.init(testing.allocator);
    defer reg.deinit();

    try testing.expect(reg.resolveAnyName("dummy") == null);

    try reg.put(testInfo("dummy", "test_driver"));
    try testing.expectEqualStrings("test_driver", reg.resolveAnyName("dummy").?);
}

test "addDependency records an edge, errors for an unregistered instance" {
    var reg = DriverRegistry.init(testing.allocator);
    defer reg.deinit();

    try reg.put(testInfo("dummy", "test_driver"));
    try reg.put(testInfo("module", "dummy2"));

    try testing.expectError(error.InstanceNotFound, reg.addDependency("module", "nonexistent", "dummy", "test_driver"));

    try reg.addDependency("module", "dummy2", "dummy", "test_driver");

    const dummy2 = reg.get("module", "dummy2").?;
    try testing.expectEqual(@as(usize, 1), dummy2.dependencies.items.len);
    try testing.expectEqualStrings("dummy", dummy2.dependencies.items[0].type_name);
    try testing.expectEqualStrings("test_driver", dummy2.dependencies.items[0].inst_name);
}
