//! The dependency wiring from kernel.ini: for a (consumer module, interface
//! type) pair, which module's export should fill that import.
//!
//! This is what replaced per-call-site "any vs. named" import resolution:
//! a module's own code (`abi.importInterface`) only ever says which
//! *type* it needs. Which concrete module provides it is data, kept here,
//! external to every module's source -- exactly so a future graphical tool
//! can read and edit it without touching (or recompiling) module code. An
//! entry is optional: a pair absent from the graph falls back to "the sole
//! registered provider of that type" (see `module_import.zig`), so
//! single-provider setups don't need to spell out every edge.
//!
//! kernel.ini syntax: each module gets its own `[module_name]` section
//! (with an `enable = true/false` line), and, if it imports anything, a
//! sibling `[module_name.dependencies]` section naming a provider module
//! per interface type it needs:
//!
//!     [hnsorens.memory.vmm]
//!     enable = true
//!     [hnsorens.memory.vmm.dependencies]
//!     mmu = hnsorens.memory.mmu
//!
//! wires the vmm module's `Mmu` import to the mmu module's export. The key
//! on each dependency line (`mmu` above) is the lowercased interface type
//! name (`Mmu` -> "mmu", `InterruptManager` -> "interruptmanager"),
//! matching what `abi.importInterface` derives at comptime. `kernel.ini`
//! parsing (in `module_loader.zig`) recovers the owning module name from
//! the `[module_name.dependencies]` section header, so this type only
//! deals in already-split (consumer_module, type_name, provider_module)
//! triples -- it has no INI-line-parsing of its own.

const std = @import("std");

const Edge = struct {
    consumer_module: []const u8,
    type_name: []const u8,
    provider_module: []const u8,
};

pub const DependencyGraph = struct {
    allocator: std.mem.Allocator,
    /// A plain list rather than a hash map keyed on "<consumer>.<type>":
    /// edge counts here are tens, not thousands, so a linear scan is free,
    /// and it sidesteps a `StringArrayHashMapUnmanaged` growing against the
    /// same boot-time bump allocator the module registry's own map does
    /// (see the comment on `DriverRegistry.module_owner`).
    edges: std.ArrayList(Edge) = .empty,

    pub fn init(allocator: std.mem.Allocator) DependencyGraph {
        return .{ .allocator = allocator };
    }

    pub fn deinit(self: *DependencyGraph) void {
        self.edges.deinit(self.allocator);
    }

    pub fn addEdge(self: *DependencyGraph, consumer_module: []const u8, type_name: []const u8, provider_module: []const u8) !void {
        try self.edges.append(self.allocator, .{ .consumer_module = consumer_module, .type_name = type_name, .provider_module = provider_module });
    }

    pub fn resolve(self: *const DependencyGraph, consumer_module: []const u8, type_name: []const u8) ?[]const u8 {
        for (self.edges.items) |edge| {
            if (std.mem.eql(u8, edge.consumer_module, consumer_module) and std.mem.eql(u8, edge.type_name, type_name)) {
                return edge.provider_module;
            }
        }
        return null;
    }
};

const testing = std.testing;

test "addEdge records an edge, resolve looks it up by (consumer, type)" {
    var graph = DependencyGraph.init(testing.allocator);
    defer graph.deinit();

    try graph.addEdge("hnsorens.memory.vmm", "mmu", "hnsorens.memory.mmu");
    try testing.expectEqualStrings("hnsorens.memory.mmu", graph.resolve("hnsorens.memory.vmm", "mmu").?);
    try testing.expect(graph.resolve("hnsorens.memory.vmm", "pmm") == null);
}
