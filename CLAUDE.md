# ARM_OS — guidance for Claude

This is a UEFI-booted AArch64 micro-OS. A Zig bootloader loads **modules**
(freestanding AArch64 PIE ELFs) into the high half, links them by a vtable
ABI, initializes them in dependency order, and runs their tests in QEMU.

## Do NOT read the whole tree

The module ABI is small and uniform. To add or change a module you need
**only** these files:

1. `modules/README.md` — the full authoring spec. Read this first.
2. `shared/abi_types.zig` — every cross-module interface type + errno/flag
   constants. This is the contract.
3. `shared/abi.zig` — the module-facing re-export + `exportInterface` /
   `importInterface` / `declareConfig*` / `kernelTest`.
4. **One existing module as a template**, matched to what you're building:
   - allocator / pure-logic → `modules/hnsorens/memory/pmm/{main,test}.zig`
   - MMIO device + IRQ + raw asm vector table → `modules/hnsorens/io/gic_v3/main.zig`
   - system registers + interrupt-driven service → `modules/hnsorens/io/timer/main.zig`
   - imports another module, test-only → `modules/dummy2/main.zig`
   - config values → `modules/configtest/main.zig`
5. `kernel.ini` — the boot manifest you add a section to.

Shared helpers you can `@import` (don't re-read unless using a new API):
`kernel_test`, `kernel_fmt`, `mmio`, `sysreg`, `spinlock`, `phys_mem`.

Only go wider (bootloader internals in `bootloader/modules/*.zig`) if you
are changing **how modules are loaded/linked**, not when writing one.

Prefer the `code-review-graph` MCP tools over grep when you do need to
explore (see the repo-parent `CLAUDE.md`).

## Module facts that are easy to get wrong

- Directory path under `modules/` **is** the module name (`/`→`.`).
- Imported interfaces and config placeholders are `undefined` until the
  module's `main()` runs. Never dereference them at module scope /
  comptime. They ARE valid inside `main()` and inside tests.
- Vtable fns are `pub fn f(...) callconv(.c) <ret>`; return `0` / positive
  `errno`. Out-params are trailing pointers. No allocation crosses the ABI.
- New interface type → add `extern struct` (+ constants) to
  `shared/abi_types.zig`, then a re-export line in `shared/abi.zig`.
  Keep `abi_types.zig` free of side-effectful exports (bootloader imports it).
- `main(boot_info_ptr: *anyopaque)` runs **once** and must **return**.
- Modules run at **EL1**, MMU on. `TTBR0` = flat identity map of phys;
  `TTBR1` = HHDM (`virt = phys + abi.HHDM_OFFSET`) + module load region.
  No EL0/userspace exists yet.
- `gic_v3` currently owns `vbar_el1` and every vector slot branches to the
  IRQ path — synchronous exceptions (SVC, aborts) are not handled yet.

## Build / test

```sh
zig build qemu-test   # boots ALL enabled modules in QEMU, runs their kernelTests (~minutes)
zig build unit-test   # fast native tests (bootloader logic only)
zig build test        # both
```

Pass/fail = `tools/qemu_test_check.zig` scanning `qemu-test-output.log`
for `[SUMMARY] passed=N failed=N skipped=N` (fails on `failed>0`, any
`TEST FAIL` / `FAIL]` line, or no summary at all). Baseline before this
line of work: `passed=295 failed=0 skipped=1`.

To iterate fast on one module, set `enable = false` on unrelated modules
in `kernel.ini` (keep dependencies enabled), then restore before commit.

## Test bar

Match `pmm/test.zig` density: happy path, **every** error path, edge /
overflow inputs, a stress/churn loop, and an invariant that must hold
across churn. One `kernel_test.Tracker` per test fn, `return t.result()`.

## Userspace build-out (in progress)

Bottom-up module plan, each landed and tested before the next:

1. `hnsorens.sched.context_switch` — `TaskContext` + naked-asm `switch_to`.
2. `hnsorens.arch.exceptions` — real full vector table (owns `vbar_el1`);
   `gic_v3` refactored to register its IRQ dispatch through it. Sync
   exception + fault (data/instruction abort) hooks.
3. `hnsorens.proc.process` — PCB/TCB, address space (via `vmm`), kernel
   stacks, state machine.
4. `hnsorens.sched.scheduler` — round-robin ready queue, tick preemption
   via `timer`, `yield`/`block`/`wake`.
5. `hnsorens.sys.syscall` — `SVC` dispatch, syscall table, the syscall-map
   / masking mechanism (userspace-visible number → kernel handler slot).
6. `hnsorens.proc.elf_loader` — load a userspace ELF from `vfs`/`ext2`
   into a process address space, build its stack, drop to EL0.
7. C userland: `crt0` + minimal libc (`tools/` build), `/bin/sh`, coreutils.

See `things_to_add/*.c` (in git history) and `../HendOS` for reference
implementations of processes, syscalls, ext2 loading, and the shell.
