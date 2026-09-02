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
| `context_switch` | sched | `ContextSwitch` | Raw AArch64 cooperative register-file switching: `TaskContext` (x19–x28, fp, lr, sp), naked-asm `switch_to`/`jump_to`, `init_kernel_context` to build a fresh task. IRQ-tolerant (no window where SP is invalid). No C reference (HendOS's was x86_64 iretq-frame based) — follows the AArch64 plan below. |
| `exceptions` | arch | `Exceptions` | Owns VBAR_EL1 + a full 16-entry vector table with `TrapFrame` save/restore. `register_handler(vector, cb)` per class: `sync_svc` / `sync_data_abort` / `sync_instruction_abort` / `pc_alignment` / `sp_alignment` / `sync_other` / `irq` / `fiq` / `serror`. `gic_v3` was refactored to drop its private IRQ table + VBAR write and register an `.irq`/`.fiq` callback here instead. Default policy: resume for SVC/async, halt-with-log for an unhandled real fault. |
| `process` | proc | `Process` | Fixed 64-slot TCB table. `create_kernel_thread` (kernel stack from `pmm` via its HHDM alias, `TaskContext` built through `context_switch`), state machine (new/ready/running/blocked/zombie/dead), priority, exit code, `destroy`/reap, `list`. Kernel threads only for now (user address spaces come with the ELF loader). |
| `scheduler` | sched | `Scheduler` | Cooperative round-robin over a ready-pid ring buffer. `admit`/`remove`/`yield`/`block`/`wake`/`exit_current`; `run` switches into the first task and returns to its caller once the queue drains. No tick preemption yet (needs a reschedule-after-EOIR hook in the exception path). Mirrors HendOS `scheduler.c` semantics, queue-based. |
| `syscall` | sys | `Syscalls` | Fixed 512-slot dispatch table. `register(nr, handler)` / `unregister` / `invoke` (direct kernel call) / `is_registered` / `count`. Registers one `.sync_svc` callback with `exceptions`; reads nr from x8, args x0..x5, writes result to x0 (AArch64 Linux convention). Numbers spec'd centrally in `abi_types.zig` (`SYS_*`, matching Linux/aarch64). No remap/mask layer (deferred). |

## Not started yet

### Time & scheduling (the next big subsystem)

- ~~**Context switcher** (`sched/context_switch`)~~ — **DONE** (see the
  table above). Kept the `TaskContext` to the callee-saved half only;
  ELR/SPSR live in the exception trap frame (built by the exceptions
  module, next) and TTBR0 switching stays `Mmu.set_user_ctx`'s job.
- ~~**Thread control manager**~~ — **DONE** as `hnsorens.proc.process`
  (kernel threads; user address spaces still TODO, with the ELF loader).
- ~~**Round-robin scheduler**~~ — **DONE** as `hnsorens.sched.scheduler`,
  cooperative. Still TODO: **tick-driven preemption** — needs a
  reschedule hook that runs *after* `gicIrqCallback`'s EOIR (otherwise the
  timer IRQ stays active on the preempted task's stack and no further
  ticks land). Cleanest: one more optional callback slot in the exception
  dispatch path that the scheduler registers.
- **Kernel synchronization primitives** (`sync/*`) — spinlock (real one;
  every C module so far used a no-op stub since there was only ever one
  core running), mutex, semaphore, condvar-equivalent. Needed the moment
  more than one thread can touch shared kernel state.

### IPC & userspace

- **Inter-Process Communication** (`ipc/*`) — message queues or
  synchronous send/receive between threads/processes; needed before any
  real multi-process design (servers, drivers-as-processes, etc.) makes
  sense.
- **Syscall interface** — the `SVC` synchronous-exception vector now
  exists (`hnsorens.arch.exceptions`, `.sync_svc` class; a callback can
  already read args from / write the return value into the `TrapFrame`).
  Still needed: a `hnsorens.sys.syscall` module owning a **fixed** syscall
  number → handler table at spec'd indices, argument marshaling, and the
  EL0 entry once processes exist. (Userspace syscall remap/mask layer:
  deferred — add later as its own piece.)
- **User-mode ELF loader** — distinct from the existing bootloader module
  loader (which loads *kernel* modules pre-MMU-setup, at fixed
  high-canonical addresses). This one loads a userspace binary into a
  process's own `vmm` address space, sets up its stack, and drops to EL0.
- **Exception/fault handlers** — the vector table + dispatch + trap frame
  now exist (`hnsorens.arch.exceptions`). What's left: a `.sync_data_abort`
  / `.sync_instruction_abort` callback (in the process/fault module) that
  turns a lower-EL fault into task termination (SIGSEGV-equivalent) and an
  EL1 fault into a proper kernel panic, and eventually demand-paging /
  copy-on-write via `vmm`+`mmu` off the same hook.

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
