//! AArch64 4-level (4KB granule) stage-1 page tables, built while boot
//! services are still active (each new table level is allocated via
//! `AllocatePages`), plus the register programming needed to turn the MMU on.

const std = @import("std");
const uefi = std.os.uefi;

const PAGE_SIZE: u64 = 4096;

pub const PageOrder = enum(u2) {
    four_kb = 0,
    two_mb = 1,
    one_gb = 2,

    fn sizeBytes(self: PageOrder) u64 {
        return switch (self) {
            .four_kb => PAGE_SIZE,
            .two_mb => PAGE_SIZE * 512,
            .one_gb => PAGE_SIZE * 512 * 512,
        };
    }
};

const Indices = struct {
    p0: u9,
    p1: u9,
    p2: u9,
    p3: u9,

    fn extract(va: u64) Indices {
        return .{
            .p0 = @truncate(va >> 39),
            .p1 = @truncate(va >> 30),
            .p2 = @truncate(va >> 21),
            .p3 = @truncate(va >> 12),
        };
    }
};

pub const Pte = packed struct(u64) {
    valid: bool, // Bit 0
    is_table_or_page: bool, // Bit 1: table/4KB-page descriptor vs 1GB/2MB block
    attr_indx: u3, // Bits 2-4: index into MAIR_EL1
    ns: bool, // Bit 5
    ap: u2, // Bits 6-7: 00 = Kernel R/W, no EL0 access
    sh: u2, // Bits 8-9: 11 = Inner Shareable
    af: bool, // Bit 10: Access Flag (set to avoid access faults)
    ng: bool, // Bit 11
    frame_address: u36, // Bits 12-47: physical address >> 12
    reserved: u4 = 0, // Bits 48-51
    contiguous: bool = false, // Bit 52
    pxn: bool = false, // Bit 53
    uxn_or_xn: bool = false, // Bit 54
    soft_reserved: u4 = 0, // Bits 55-58
    pbha: u4 = 0, // Bits 59-62
    ignored: bool = false, // Bit 63

    const ATTR_IDX_NORMAL_WB: u3 = 0;

    /// Points to another page table level (or, at level 3, is itself a leaf
    /// 4KB page descriptor -- same bit pattern, meaning depends on level).
    fn newTable(table_ptr: *Ptp) Pte {
        return tableOrLeaf(@intFromPtr(table_ptr), true);
    }

    /// Leaf entry mapping a block/page of physical memory as normal,
    /// cacheable, kernel-only read/write memory.
    fn newLeaf(phys_addr: u64, order: PageOrder) Pte {
        return tableOrLeaf(phys_addr, order == .four_kb);
    }

    fn tableOrLeaf(addr: u64, is_table_or_page: bool) Pte {
        return .{
            .valid = true,
            .is_table_or_page = is_table_or_page,
            .attr_indx = ATTR_IDX_NORMAL_WB,
            .ns = false,
            .ap = 0,
            .sh = 3,
            .af = true,
            .ng = false,
            .frame_address = @truncate(addr >> 12),
        };
    }

    pub fn getTablePointer(self: Pte) *Ptp {
        const addr: usize = @as(u64, self.frame_address) << 12;
        return @ptrFromInt(addr);
    }
};

comptime {
    std.debug.assert(@bitSizeOf(Pte) == 64);
}

pub const Ptp = extern struct {
    entries: [512]Pte align(4096),

    comptime {
        std.debug.assert(@sizeOf(Ptp) == 4096);
        std.debug.assert(@alignOf(Ptp) == 4096);
    }

    pub fn zero(self: *Ptp) void {
        const bytes: *[@sizeOf(Ptp)]u8 = @ptrCast(self);
        @memset(bytes, 0);
    }
};

/// Allocates a fresh, zeroed, page-aligned table using UEFI AllocatePages.
/// Uses `.runtime_services_code` so the allocation survives ExitBootServices
/// classified as reserved-for-us memory (matches the C bootloader's
/// KEEP_AFTER_BOOT = EfiRuntimeServicesCode).
fn allocTable() !*Ptp {
    const bs = uefi.system_table.boot_services.?;
    const pages = try bs.allocatePages(.any, .runtime_services_code, 1);
    const table: *Ptp = @ptrCast(pages.ptr);
    table.zero();
    return table;
}

fn getOrCreateTable(entry: *Pte) !*Ptp {
    if (!entry.valid) {
        const table = try allocTable();
        entry.* = Pte.newTable(table);
        return table;
    }
    return entry.getTablePointer();
}

pub const PageTable = struct {
    root: ?*Ptp = null,

    /// Maps `count` pages of `order` size, starting at `virt_start` /
    /// `phys_start`, allocating any missing intermediate table levels.
    pub fn mapMemory(self: *PageTable, virt_start: u64, phys_start: u64, order: PageOrder, count: u64) !void {
        if (self.root == null) {
            self.root = try allocTable();
        }
        const root_table = self.root.?;
        const step = order.sizeBytes();

        var i: u64 = 0;
        while (i < count) : (i += 1) {
            const virt = virt_start + i * step;
            const phys = phys_start + i * step;
            const idx = Indices.extract(virt);

            const l1 = try getOrCreateTable(&root_table.entries[idx.p0]);
            if (order == .one_gb) {
                if (!l1.entries[idx.p1].valid) l1.entries[idx.p1] = Pte.newLeaf(phys, .one_gb);
                continue;
            }

            const l2 = try getOrCreateTable(&l1.entries[idx.p1]);
            if (order == .two_mb) {
                if (!l2.entries[idx.p2].valid) l2.entries[idx.p2] = Pte.newLeaf(phys, .two_mb);
                continue;
            }

            const l3 = try getOrCreateTable(&l2.entries[idx.p2]);
            if (!l3.entries[idx.p3].valid) l3.entries[idx.p3] = Pte.newLeaf(phys, .four_kb);
        }
    }

    /// Builds a flat identity map (virt == phys) of the first `total_memory`
    /// bytes, in 1GB blocks. Used for the low half (TTBR0): everything the
    /// bootloader and firmware are already running from/using (its own
    /// code/stack, MMIO like the UART) needs to stay reachable at the same
    /// address once the MMU is on.
    pub fn createIdentity(total_memory: u64) !PageTable {
        var pt = PageTable{};
        const gb: u64 = 1024 * 1024 * 1024;
        const blocks = (total_memory + gb - 1) / gb;
        try pt.mapMemory(0, 0, .one_gb, blocks);
        return pt;
    }
};

// --- TCR_EL1 / MAIR_EL1 / SCTLR_EL1 field layout ---

const TCR_IPS_SHIFT = 32;
const TCR_TG1_SHIFT = 30;
const TCR_SH1_SHIFT = 28;
const TCR_ORGN1_SHIFT = 26;
const TCR_IRGN1_SHIFT = 24;
const TCR_T1SZ_SHIFT = 16;
const TCR_TG0_SHIFT = 14;
const TCR_SH0_SHIFT = 12;
const TCR_ORGN0_SHIFT = 10;
const TCR_IRGN0_SHIFT = 8;
const TCR_T0SZ_SHIFT = 0;

const TCR_IPS_40BIT: u64 = 0b10;
const TCR_TG_4KB: u64 = 0b00;
const TCR_SH_INNER: u64 = 0b11;
const TCR_RGN_WB: u64 = 0b01;
const TCR_TXSZ_48BIT: u64 = 64 - 48;

const MAIR_NORMAL_WB: u64 = 0xFF;
const MAIR_DEVICE_nGnRE: u64 = 0x04;
const MAIR_IDX_NORMAL = 0;
const MAIR_IDX_DEVICE = 1;

const SCTLR_M_ENABLE: u64 = 1 << 0;
const SCTLR_C_ENABLE: u64 = 1 << 2;
const SCTLR_I_ENABLE: u64 = 1 << 12;

/// Programs MAIR_EL1/TCR_EL1/TTBR{0,1}_EL1, invalidates the TLB, and turns
/// the MMU + caches on. `lower` (TTBR0) covers low/identity addresses,
/// `upper` (TTBR1) covers the high half where modules and the boot stack
/// live.
pub fn enableTranslation(lower: *const PageTable, upper: *const PageTable) void {
    const mair: u64 = (MAIR_NORMAL_WB << (MAIR_IDX_NORMAL * 8)) | (MAIR_DEVICE_nGnRE << (MAIR_IDX_DEVICE * 8));
    asm volatile ("msr mair_el1, %[v]"
        :
        : [v] "r" (mair),
    );

    const tcr: u64 = (TCR_IPS_40BIT << TCR_IPS_SHIFT) |
        (TCR_TG_4KB << TCR_TG1_SHIFT) |
        (TCR_SH_INNER << TCR_SH1_SHIFT) |
        (TCR_RGN_WB << TCR_ORGN1_SHIFT) |
        (TCR_RGN_WB << TCR_IRGN1_SHIFT) |
        (TCR_TXSZ_48BIT << TCR_T1SZ_SHIFT) |
        (TCR_TG_4KB << TCR_TG0_SHIFT) |
        (TCR_SH_INNER << TCR_SH0_SHIFT) |
        (TCR_RGN_WB << TCR_ORGN0_SHIFT) |
        (TCR_RGN_WB << TCR_IRGN0_SHIFT) |
        (TCR_TXSZ_48BIT << TCR_T0SZ_SHIFT);
    asm volatile ("msr tcr_el1, %[v]"
        :
        : [v] "r" (tcr),
    );

    asm volatile ("msr ttbr0_el1, %[v]"
        :
        : [v] "r" (@intFromPtr(lower.root.?)),
    );
    asm volatile ("msr ttbr1_el1, %[v]"
        :
        : [v] "r" (@intFromPtr(upper.root.?)),
    );

    asm volatile ("dsb sy");
    asm volatile ("tlbi vmalle1");
    asm volatile ("dsb sy");
    asm volatile ("isb");

    var sctlr: u64 = asm volatile ("mrs %[out], sctlr_el1"
        : [out] "=r" (-> u64),
    );
    sctlr |= SCTLR_M_ENABLE | SCTLR_C_ENABLE | SCTLR_I_ENABLE;
    asm volatile ("msr sctlr_el1, %[v]"
        :
        : [v] "r" (sctlr),
    );
    asm volatile ("isb");

    // Module code was memcpy'd into these physical pages by ordinary data
    // writes before caches were on; nothing has ever told the CPU's (or
    // QEMU TCG's) instruction cache/translation cache that memory in that
    // range now holds real instructions. Invalidate the whole I-cache
    // before anything jumps into freshly loaded code.
    asm volatile ("ic ialluis");
    asm volatile ("dsb ish");
    asm volatile ("isb");
}

// --- Unit tests -------------------------------------------------------
//
// Only the bit-packing/index math is tested here: everything else in this
// file either issues AArch64 system-register instructions (not valid on
// the native test target) or calls UEFI boot services (not available
// there). That logic is exercised by the QEMU integration test instead.

const testing = std.testing;

test "Indices.extract splits a canonical VA into four 9-bit table indices" {
    // The address the "dummy" module's entry point actually landed at
    // during bring-up: VIRTUAL_MODULE_LOAD_START + 0x138024.
    const idx = Indices.extract(0xFFFFFF8000138024);
    try testing.expectEqual(@as(u9, 0x1FF), idx.p0);
    try testing.expectEqual(@as(u9, 0), idx.p1);
    try testing.expectEqual(@as(u9, 0), idx.p2);
    try testing.expectEqual(@as(u9, 0x138), idx.p3);
}

test "Pte.newLeaf round-trips a page-aligned physical address through frame_address" {
    const phys: u64 = 0x43DEC6000;
    const pte = Pte.newLeaf(phys, .four_kb);
    try testing.expect(pte.valid);
    try testing.expectEqual(phys, @as(u64, pte.frame_address) << 12);
}

test "Pte.newLeaf uses the page-descriptor bit only at 4KB (level 3)" {
    // Levels 1/2 need a block descriptor (bit 1 clear); only a level-3 leaf
    // is a page descriptor (bit 1 set) -- same encoding as a table
    // descriptor, disambiguated purely by which level it's found at.
    try testing.expect(Pte.newLeaf(0x40000000, .four_kb).is_table_or_page);
    try testing.expect(!Pte.newLeaf(0x40000000, .two_mb).is_table_or_page);
    try testing.expect(!Pte.newLeaf(0x40000000, .one_gb).is_table_or_page);
}

test "Pte.newTable / getTablePointer round-trip" {
    var table: Ptp = undefined;
    table.zero();
    const pte = Pte.newTable(&table);
    try testing.expect(pte.valid);
    try testing.expect(pte.is_table_or_page);
    try testing.expectEqual(@as(*Ptp, &table), pte.getTablePointer());
}

test "Ptp.zero clears every entry" {
    var table: Ptp = undefined;
    @memset(std.mem.asBytes(&table), 0xAA);
    table.zero();
    for (table.entries) |entry| {
        try testing.expectEqual(@as(u64, 0), @as(u64, @bitCast(entry)));
    }
}
