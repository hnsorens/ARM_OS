# Writing a kernel module

Every piece of this OS above the bootloader is a **module**: a freestanding
AArch64 PIE executable that the bootloader relocates into the high half of
the address space, links against the modules it depends on, initializes
once, and tests. This document is the full spec for authoring one.

If you just want the checklist, jump to [Recipe](#recipe).

---

## 1. What a module is

A module is a directory under `modules/` containing at minimum a
`main.zig`. The build system (`modules/build.zig`) discovers it by walking
for `main.zig` files, so **the directory path is the module name**, with
`/` replaced by `.`:

| Directory | Module name (used in `kernel.ini`) |
|---|---|
| `modules/dummy/main.zig` | `dummy` |
| `modules/hnsorens/memory/pmm/main.zig` | `hnsorens.memory.pmm` |
| `modules/hnsorens/sched/context_switch/main.zig` | `hnsorens.sched.context_switch` |

By convention everything real lives under `modules/hnsorens/<category>/<name>/`.
Categories in use: `io`, `memory`, `storage`, `fs`, `sched`, `proc`, `arch`,
`sys`. Pick the one that fits; make a new one if none do.

A module directory normally holds three files:

```
modules/hnsorens/<cat>/<name>/
    main.zig      required — entry point, exports, imports, config
    test.zig      the module's kernelTests (pulled in by main.zig)
    module.ini    human/tooling description of the interface (not read at boot)
```

---

## 2. The ABI

All cross-module types live in **`shared/abi_types.zig`** (plain data, no
side effects — the bootloader imports this directly) and are re-exported
from **`shared/abi.zig`** (the module-facing surface, which also carries
the `_start` shim and the `export`/`import`/`config`/`test` machinery).

Inside a module you only ever `@import("abi")`.

### 2.1 Interface types

An interface is an `extern struct` of `*const fn (...) callconv(.c) ...`
pointers — a vtable. Signatures mirror the C `include/api/*.h` headers.
Conventions that hold everywhere:

- **Return `c_int`**: `0` on success, a positive `errno` (`abi.EINVAL`,
  `abi.ENOMEM`, …, defined in `abi_types.zig`) on failure. Functions that
  genuinely cannot fail may return `void`, `u64`, etc.
- **Out-params are pointers**, passed last: `alloc_page(order: u8, out_frame: *u64)`.
- **No allocation crosses the boundary.** If a caller needs a buffer
  filled, it passes the buffer and a capacity; the callee never returns
  owned memory.
- **Opaque handles are `?*anyopaque`** (e.g. a heap, an ext2 mount). The
  owning module is the only one that knows the real type.
- **Everything is `extern struct` / `enum(uN)` / fixed arrays** — no Zig
  slices, optionals, or error unions in a type that appears in a vtable.

To add a new interface:

1. Add the `extern struct` to `shared/abi_types.zig`, with a doc comment
   describing ownership and lifetime of every pointer it passes.
2. Add a `pub const Foo = shared.Foo;` line to `shared/abi.zig`.
3. If it needs new constants (errno-likes, flag bits, magic numbers),
   put those in `abi_types.zig` too and re-export them.

The lowercased struct name is the **category** used for wiring
(`InterruptManager` → `interruptmanager`, `Pmm` → `pmm`).

### 2.2 Exporting an interface

In a `comptime` block in `main.zig`:

```zig
comptime {
    abi.exportInterface("buddy", abi.Pmm, .{
        .alloc_page = allocPage,
        .alloc_aligned = allocAligned,
        // ... every field of abi.Pmm ...
    });
}
```

`"buddy"` is this instance's name. A module may export more than one
interface; each call needs a distinct instance name. The vtable lands in
an ELF section (`.kmodule.export.pmm.buddy`) the bootloader scans.

Exported functions that appear in the vtable must be
`pub fn name(...) callconv(.c) <ret>`.

### 2.3 Importing an interface

At module scope (not inside a function):

```zig
pub const serial_if = abi.importInterface(abi.Serial);
pub const pmm_if = abi.importInterface(abi.Pmm);
```

This returns a `*const abi.Serial` **placeholder**. It is `undefined`
until the bootloader patches it in — do **not** dereference it before
your `main()` runs (module scope initializers, `comptime`, etc. are too
early). Inside `main()` and anything it calls (including tests, which run
after `main()`), it is live: `serial_if.write(ptr, len)`.

Which module fills the placeholder is resolved from `kernel.ini`:

- If exactly one loaded module exports that category, it is used
  automatically — no `kernel.ini` entry needed.
- If more than one could provide it, you **must** add a dependency line
  (see §4). Ambiguity is a boot failure, not a silent pick.

### 2.4 Config values

A module can take typed scalars from `kernel.ini`:

```zig
const queue_size  = abi.declareConfigInt("queue_size", u32, 8);      // default 8
const debug_on    = abi.declareConfigBool("debug_on", false);
const label       = abi.declareConfigStr("label", 32, "root");        // max 32 bytes
```

Each returns a `*const T` placeholder with the **compiled-in default**,
overwritten in place by the bootloader with the `kernel.ini`
`[<module>.config]` value if one is set. Same rule as imports: not valid
until `main()` runs. Read with `.*` (for strings,
`std.mem.sliceTo(label, 0)`).

---

## 3. `main.zig` anatomy

```zig
//! One-paragraph doc comment: what this module is, what it exports, any
//! notable design choice or ported-from-C caveat.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const sysreg = @import("sysreg");     // only what you use — see §6

// --- imports (module scope, never dereferenced before main) ---
pub const serial_if = abi.importInterface(abi.Serial);

// --- module state: `var s_*` / `var g_*` globals, zero-initialized ---
var s_state: Foo = .{};

// --- the exported vtable functions ---
pub fn doThing(arg: u64, out: *u64) callconv(.c) c_int {
    // ...
    return 0;
}

// --- init: called once, in dependency order, with the boot info ptr ---
pub fn main(boot_info_ptr: *anyopaque) void {
    const boot_info: *abi.BootInfo = @ptrCast(@alignCast(boot_info_ptr));
    _ = boot_info;
    // set up hardware, allocate tables, etc.
}

comptime {
    abi.exportInterface("myinst", abi.Foo, .{ .do_thing = doThing });
}

comptime {
    _ = @import("test.zig");   // pulls the tests into the build
}
```

Notes:

- `main` is optional. A module with no `main` (and no export) is a
  **test-only module** — it imports other modules and only runs tests.
  The bootloader calls such modules' tests in a second pass.
- `main` must **return**. It is a one-shot init routine, not a loop. The
  scheduler (once it exists) is what will run things forever.
- Modules currently run at **EL1**, MMU on: `TTBR0` is a flat identity map
  of physical memory, `TTBR1` has the HHDM (`virt = phys + abi.HHDM_OFFSET`)
  and the module load region. There is no userspace yet — that's what the
  `proc` / `sys` / `arch` modules are being built to add.
- `boot_info.memory_regions[0..memory_map_size]` is the physical memory
  map. `abi.HHDM_OFFSET` converts phys↔virt for direct access.

### Raw assembly

Inline `asm` and `comptime { asm(...) }` blocks are fine and used
throughout (`gic_v3` builds its exception vector table this way,
`spinlock.zig` is all `ldaxr`/`stlxr`). For system registers prefer the
declarative `sysreg.Reg` helper over hand-written `mrs`/`msr`.

---

## 4. Wiring it into `kernel.ini`

`kernel.ini` (repo root) is the boot manifest. Add a section per module:

```ini
[hnsorens.sched.context_switch]
enable = true
[hnsorens.sched.context_switch.dependencies]
serial = hnsorens.io.serial_debug
```

- `[<name>]` with `enable = true` loads it. `false` / absent → skipped.
- `[<name>.dependencies]` maps **`<category> = <provider module name>`**.
  The key is the lowercased ABI struct name (`Serial` → `serial`,
  `Pmm` → `pmm`, `InterruptManager` → `interruptmanager`). The value is
  another module's `[<name>]`.
- Only add a dependency line when the category has more than one possible
  provider, or to be explicit. A sole provider resolves automatically.
- `[<name>.config]` sets `key = value` lines matching `declareConfig*`.

Module load order in the file doesn't matter for correctness —
initialization runs in dependency order — but keep sections grouped by
category for readability.

There is also a `module.ini` **inside the module directory**. It is *not*
read at boot; it's a static mirror of the module's provides/requires for
humans and tooling. Copy the format from a neighbor.

---

## 5. Tests

Tests are the point. They run **for real in QEMU** on real hardware
state, not in a host test runner. Put them in `test.zig`:

```zig
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testSomething() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.doThing(1, &out), 0);
    t.expectNotEqual(@src(), main.doThing(0, &out), 0);   // rejects bad input
    return t.result();
}

comptime {
    abi.kernelTest("something", &testSomething);
}
```

- A test is `fn () callconv(.c) i32` returning `abi.TEST_PASS` /
  `TEST_FAIL` / `TEST_SKIP`. Use a `kernel_test.Tracker` and `return
  t.result()` so one test can make many assertions and still report a
  single pass/fail.
- `Tracker` methods: `expectEqual`, `expectNotEqual`, `expectLessThan`,
  `expectLessOrEqual`, `expectTrue`, `expectFalse` — all take `@src()`
  first so failures print `file:line`.
- Tests run **after** the module's `main()`, in registration order,
  after every dependency is initialized. Imported interfaces are live.
- Aim for the same density as `modules/hnsorens/memory/pmm/test.zig`:
  happy path, every error path, edge/overflow inputs, stress/churn loops,
  and an invariant that must hold across churn (counts balance, no
  overlap, round-trips are exact).
- The harness fails the build on `failed > 0` or any `TEST FAIL` /
  `FAIL]` line, so a crash/hang mid-test (no summary printed) also fails.

---

## 6. Available imports

`modules/build.zig` wires these into every module. Import by these names:

| `@import(...)` | What |
|---|---|
| `"abi"` | `shared/abi.zig` — all interface types + export/import/config/test |
| `"kernel_test"` | `shared/kernel_test.zig` — `Tracker` assertions |
| `"kernel_fmt"` | `shared/kernel_fmt.zig` — `print(serial, fmt, args)` (std.fmt over `Serial`) |
| `"mmio"` | `shared/mmio.zig` — declarative MMIO register types (`Reg`, `IndexedField`, `RegArray`) |
| `"sysreg"` | `shared/sysreg.zig` — declarative `mrs`/`msr` (`sysreg.Reg(T, "name")`) |
| `"spinlock"` | `shared/spinlock.zig` — `SpinLock` (`ldaxr`/`stlxr`) |
| `"phys_mem"` | `shared/phys_mem.zig` — DMA-safe contiguous buffer alloc over a `Pmm` |
| `"std"` | Zig std (freestanding subset — `std.mem`, `std.fmt`, `std.elf`, …) |

If you need a new shared helper, add it to `shared/` and wire it into
`modules/build.zig` (and, if the bootloader needs it too, note that
`shared/abi_types.zig` must stay side-effect-free).

---

## 7. Building and testing

From the repo root:

```sh
zig build unit-test    # fast — native host tests (bootloader ELF/pagetable logic only)
zig build qemu-test    # full — boots the image in QEMU, runs every module's kernelTests
zig build test         # both of the above
zig build image        # just assemble disk.img
zig build run          # boot interactively in QEMU (-nographic, gdb on :1234)
```

`qemu-test` boots **every enabled module together** in one QEMU session
and runs all their tests — there is no per-module isolation. It takes a
few minutes (real VirtIO I/O through TCG). The result is decided by
`tools/qemu_test_check.zig` scanning `qemu-test-output.log` for the
`[SUMMARY] passed=N failed=N skipped=N` line.

To iterate on one module, temporarily set `enable = false` on modules you
don't need in `kernel.ini` (keep its dependencies enabled), then flip
them back before committing.

---

## Recipe

1. `mkdir -p modules/hnsorens/<cat>/<name>` — the path is the module name.
2. If you need a new cross-module type: add the `extern struct` +
   constants to `shared/abi_types.zig`, re-export from `shared/abi.zig`.
3. Write `main.zig`: doc comment, `@import("abi")`, imports at module
   scope, `var s_*` state, `pub fn ... callconv(.c)` vtable fns,
   `pub fn main(boot_info_ptr: *anyopaque) void`, `comptime`
   `exportInterface`, `comptime _ = @import("test.zig")`.
4. Write `test.zig`: happy path + every error path + edges + a
   churn/invariant test. Register each with `abi.kernelTest`.
5. Write `module.ini` (copy a neighbor's format).
6. Add `[<name>]` + `[<name>.dependencies]` to `kernel.ini`.
7. `zig build qemu-test`, read `qemu-test-output.log`, iterate until the
   summary shows your tests passing and `failed=0`.
