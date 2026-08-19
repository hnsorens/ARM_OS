# Zig rewrite — remaining modules

Status of the C→Zig kernel module rewrite. Each module lives at
`zig/modules/hnsorens/<category>/<name>/main.zig`, exports one interface
from `shared/abi_types.zig`, and ships with `abi.kernelTest`s that run for
real in QEMU via `zig build test`.

## Done (ported, tests passing in QEMU)

| Module | Category | Interface | Notes |
|---|---|---|---|
| `serial_debug` | io | `Serial` | PL011 UART driver. Formatting moved to `shared/kernel_fmt.zig` (Zig's `std.fmt`) instead of porting the C's hand-rolled printf engine — not needed here, `@cVaStart` is disabled in this toolchain anyway. |
| `gic_v3` | io | `InterruptManager` | GICv3 distributor/redistributor + real exception vector table. Fixed: none needed, ported clean. |
| `pmm` | memory | `Pmm` | Binary buddy physical page allocator. Fixed a u64 overflow in the init block-splitting scan. |
| `mmu` | memory | `Mmu` | 4-level page tables. Fixed: missing Access-Flag/Shareability bits (every mapped page would fault on first access), `protect()` clearing the valid bit on 2MB/1GB blocks. |
| `vmm` | memory | `Vmm` | BST-tracked virtual memory areas + bootstrap slab-of-slabs. Fixed a leak on partial-allocation/resize failure. |
| `heap` | memory | `Heap` | Boundary-tag first-fit allocator. Fixed: exported vtable was missing `create`/`destroy` entirely. |
| `slab` | memory | `Slab` | Fixed-size object pool, intrusive in-page freelist, tri-state full/partial/empty page queues. Fixed: `destroy_cache` never freed the cache descriptor's own page (a leak on every destroy in the C original). |
| `timer` | io | `Timer` | Software-multiplexed logical timers over the single AArch64 EL1 physical generic timer comparator; IRQ-driven re-arm to the earliest pending expiry. No C reference existed — original design, not a port. |

## Not started yet

### Time & scheduling (the next big subsystem)

- **Context switcher** (`sched/context_switch` or similar) — raw AArch64
  register-save/restore + stack-pointer swap between two execution
  contexts. Needs a `TaskContext` struct (callee-saved x19–x30, sp, elr,
  spsr, TTBR0 for address-space switch) and a naked-asm `switchTo(old, new)`
  routine, the same style already used for the bootloader's stack-switch
  trampoline and the GICv3 vector table.
- **Thread control manager** (`sched/thread` or similar) — thread
  descriptors (TCB: id, state, priority, `TaskContext`, kernel stack,
  owning address space/`vmm` root), creation/destruction, state transitions
  (ready/running/blocked/zombie).
- **Round-robin scheduler** (`sched/scheduler`) — ready queue over TCBs,
  tick-driven preemption via the `timer` module, `yield`/`block`/`wake`.
  This is what turns the kernel from "runs module init once and halts"
  (current state) into an actual multitasking OS.
- **Kernel synchronization primitives** (`sync/*`) — spinlock (real one;
  every C module so far used a no-op stub since there was only ever one
  core running), mutex, semaphore, condvar-equivalent. Needed the moment
  more than one thread can touch shared kernel state.

### IPC & userspace

- **Inter-Process Communication** (`ipc/*`) — message queues or
  synchronous send/receive between threads/processes; needed before any
  real multi-process design (servers, drivers-as-processes, etc.) makes
  sense.
- **Syscall interface** — EL0→EL1 entry point (`SVC` exception handler,
  needs a real synchronous-exception vector, which today only has the
  placeholder GICv3 IRQ path wired up), a syscall table, and argument
  marshaling. Not in the original C `TODO.md` at all, but there's no path
  to running user programs without it.
- **User-mode ELF loader** — distinct from the existing bootloader module
  loader (which loads *kernel* modules pre-MMU-setup, at fixed
  high-canonical addresses). This one loads a userspace binary into a
  process's own `vmm` address space, sets up its stack, and drops to EL0.
- **Exception/fault handlers** — the current vector table (in `gic_v3`)
  only meaningfully handles IRQ; synchronous exceptions (data/instruction
  aborts, i.e. real page faults) just fall through. A real page-fault
  handler is what would back demand-paging/copy-on-write via `vmm`+`mmu`
  eventually, and is required simply to not silently corrupt state on the
  first user-mode bug.

### Storage

- **VirtIO MMIO Transport Driver** (`io/virtio` or `drivers/virtio`) —
  generic VirtIO MMIO device discovery/negotiation/queue setup, matching
  the transport QEMU's `virt` machine exposes (already used indirectly:
  `zig/build.zig`'s `run`/`qemu-test` steps attach a `virtio-blk-device`
  to `disk.img`, but nothing in-kernel talks to it yet).
- **VirtIO Block Device Driver** (`io/virtio_blk`) — built on the
  transport driver; read/write sector requests.
- **Virtual File System** (`fs/vfs`) — mount table, path resolution,
  file-descriptor-like handle abstraction, dispatch to concrete
  filesystem drivers.
- **FAT32 Driver** (`fs/fat32`) — concrete filesystem backing the VFS,
  read (and eventually write) support over the VirtIO block driver. Also
  what the bootloader itself already reads `kernel.ini`/module `.ko`
  files from today (via UEFI's own FAT driver, pre-MMU) — this is the
  *kernel-side* equivalent for post-boot file access.

### Not in the original C `TODO.md`, but standard for "a fundamental OS"

- **Console/TTY layer** — line-buffered input, basic terminal semantics on
  top of `serial_debug`'s raw `write()`. Needed before any interactive
  shell/userspace input is meaningful.
- **RTC / wall-clock time** — QEMU `virt` exposes a PL031 RTC; nothing
  reads it yet (only the monotonic generic timer counter is touched, in
  `gic_v3`'s test).
- **Power management** — clean shutdown/reboot via PSCI `SYSTEM_OFF`/
  `SYSTEM_RESET` (already used implicitly: QEMU's own PSCI `CPU_ON` powers
  on secondary cores, seen as `Hypervisor Call`s in early trace debugging
  during bootloader bring-up — the kernel itself never calls PSCI yet).
  Also where secondary-core bring-up (SMP) would hook in, since
  `gic_v3`/`mmu` are already core-aware (`s_core_topology`, per-core
  redistributor frames) but nothing actually starts a second core.
- **Init process** — the first thing the scheduler runs once it exists;
  not a kernel module so much as the point where "kernel" hands off to
  "userspace", worth flagging since everything above is only useful in
  service of eventually reaching it.

## Suggested order

1. Context switcher → thread control → round-robin scheduler → real sync
   primitives, in that order (each depends on the previous).
2. Syscall entry + exception/fault handling — needed the moment anything
   runs at EL0.
3. IPC, then storage (VirtIO transport → block → VFS → FAT32), then
   console/RTC/power management as they come up naturally.
