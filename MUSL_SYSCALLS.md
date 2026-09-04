# Static musl userland — syscall & kernel work checklist

Target: **statically-linked musl** (`zig cc -target aarch64-linux-musl -lc`,
no dynamic loader). Enough of the Linux/aarch64 syscall ABI + kernel
plumbing to run static busybox, then GNU coreutils / `bash -c`, then
interactive bash.

Numbers are the `asm-generic/unistd.h` (aarch64) ones — the same table
`shared/abi_types.zig` already uses.

Legend: `[x]` done · `[~]` partial / stubbed · `[ ]` not started

## Real musl / BusyBox / bash run (qemu-test passed=404)

**GNU bash 5.2.37 and BusyBox 1.36.1, static-musl, run on the kernel** —
as kernelTests (`bash -c` / `busybox sh -c` scripts) and interactively:
a booted image drops to `arm-os:/# ` and `/init` execs `/bin/bash`.

Verified end to end:
- `/musltest{,2,3}` — musl libc from `_start`: crt0 → `__libc_start_main`
  → `__init_libc` / `__init_tls` / stack canary / `.init_array` → `main`;
  TLS `errno`; stdio (`fstat`/`ioctl(TCGETS)` + `writev` + `printf`);
  `malloc` (small + 256 KiB → `mmap`); `sigaction`+`kill`+catch; buffered
  file I/O on ext2; `opendir`/`readdir`; **`fork`+`execve`+`waitpid`**;
  `pipe`+`dup2` pipelines; custom `environ` across `execve`.
- `/shtest` — `/bin/busybox` + 38 hardlinked applets: a for-loop,
  arithmetic, `echo x | rev`, redirect + `cat` readback + `rm`,
  `ls /bin | wc -l`, `&&`/`||`, `test -d`, a 3-stage `tr | sort -r |
  head` pipeline.
- `/bashtest` — GNU bash: arrays + `${#a[@]}`/negative slices, `{1..5}`,
  arithmetic loops, functions, `$()` + nested `$((...))`, a 3-process
  pipeline, `${s:2:3}`/`${s^^}`, `[[ -eq && -d ]]`, `case`, a `<<EOF`
  heredoc, `read x y <<<`, `trap ... USR1` + `kill -USR1 $$`,
  `( ... ) & wait` (background job + reap), `grep`/`wc -l < file`.
- Interactive bash (piped console input, real UART RX IRQ): prompt +
  line editing, `pwd`/`id`, `$((...))`, `for` loops, `ls /bin | wc -l`,
  `echo x | rev`, `exit` → `logout`.

Build notes (see `userland/prebuilt/README`): `zig cc -target
aarch64-linux-musl` cross, `--image-base=0x1000000000`. `userland/crt0.c`
supplies a dummy DT_NULL `_DYNAMIC` so musl's stock crt1 self-reloc
`adrp _DYNAMIC` is in range at the 64 GiB base. busybox needs
`scripts/trylink`'s `INFO_OPTS` no-op'd (zig cc rejects
`-Wl,--warn-common`/`-Map`/`--verbose`); bash needs the cross
config-cache + `CFLAGS_FOR_BUILD=-std=gnu17` for its K&R host tools.

Kernel bugs found and fixed getting here:
- `clone()` child had no `elf_loader` image record → `execve` from it
  returned EINVAL (every shell command) (`0ae4979`).
- forked child's anon-mmap allocator didn't inherit `mmap_top`/`brk_cur`
  → post-fork `malloc` re-`mmap`'d an occupied VA → NULL →
  "bash: xmalloc: cannot allocate 2048 bytes" (`4831073`).
- syscall handlers run with IRQs masked (SVC entry), so `ppoll(tty)`
  from readline spun forever — the UART RX IRQ could never fire.
  SpinLock made IRQ-safe; busy-wait syscalls re-enable IRQ in their
  spin (`b8c110b`).

## STATUS (qemu-test passed=404)

Done: FP/SIMD + TPIDR_EL0 + auxv (Layer 0); anon mmap/munmap/mprotect +
brk; the full coreutils syscall surface (stat family, getdents64, fcntl,
ioctl, the `*at` family, writev/readv, time, id stubs, Tier C stubs);
pipe2 + per-process cwd + `/dev` nodes; statfs/fstatfs/mknodat;
**`O_CLOEXEC`/`FD_CLOEXEC` across execve** (`9a30f53`); **real
`nanosleep`/`clock_nanosleep`** (`e5ec6ed`); **Layer 3 signals**
(rt_sigaction/procmask/pending/**suspend**/return, `sigaltstack`
store-only, delivery via the exceptions return-to-EL0 hook, kill family,
SIGSEGV-from-fault, SIGCHLD, SIGPIPE, default actions, fork inherit); and
**job control** — real pgid/sid, termios driving the tty, foreground
process group, and `SIGSTOP`/`SIGTSTP`/`SIGCONT` stop/continue with
`wait4(WUNTRACED)`.

The layout heisenbug that gated all of this is **fixed** (`e223534`):
`process.count()`/`list()` iterated the 64-entry TCB table by value,
spilling a ~48 KiB copy onto the caller's 32 KiB syscall stack, which
overflowed into a live page table. Regression test
`process:list_does_not_copy_table_to_stack`.

Two exception-path bugs found chasing `nanosleep` and fixed (`9332353`):
`exc_common` did not save/restore **q0..q31 / fpsr / fpcr**, so any EL0
caller holding a value in a vector register across `svc` (every -O2
libc: struct copies, printf) got it silently corrupted; and it restored
**SP_EL0** unconditionally, clobbering the interrupted task's user SP
when an IRQ landed in a long-running syscall (now gated on
`SPSR.M[3:0]==0`).

Also landed since: **`ppoll`** + **`pselect6`** real (pipe/tty/file
readiness, blocking with a cntvct deadline); **`SA_RESTART`** (blocking
syscall → internal `-ERESTARTSYS`; the delivery hook rewinds the `svc`
for an SA_RESTART handler / spurious wake, else rewrites x0 → `-EINTR`);
wired for `ppoll`/`pselect6`/`wait4`.

Not done: `rt_sigtimedwait` / `rt_sigqueueinfo`; `epoll`/`timerfd`/
`eventfd` (no threads/async here — `futex` is a 0-stub that works
single-threaded); `SIGTTIN`/`SIGTTOU` when a background pgrp touches the
tty (needs an error return on `tty.read`); `wait4` `WCONTINUED`;
`statx`/`waitid` deliberately unregistered so musl falls back;
`sigaltstack` round-trips but delivery never switches stacks; no wall
clock (CLOCK_REALTIME == CLOCK_MONOTONIC).

Static busybox / coreutils / `sh -c` and line-edited interactive shells
should run on what's landed; the remaining gaps are ENOSYS-/degradation-
tolerant.

~110 syscalls registered across `fd`, `elf_loader`, `process`, `signal`,
`tty`.

### `nanosleep` implementation notes

Spin-to-deadline against `cntvct_el0` (`e5ec6ed`) — no sleep queue yet,
the task busy-waits with a `yield` hint, bailing `EINTR` (+ remainder)
if `signal.has_pending(pid)`. Fine for `sleep`/retry loops; wastes a
core for the sleep duration when it is the only runnable task. A real
sleep queue (`sched.block()` + one-shot timer `wake()`) also needs the
scheduler run-loop to survive a drained queue (today `run()` just
returns), so it is deferred until there is a persistent idle task.

---

## Layer 0 + first batch — DONE

- [x] `read`/`write`/`openat`/`close`/`lseek`/`dup`, `getpid`/`getppid`/
      `sched_yield`, `exit`/`exit_group`/`wait4` (+WNOHANG),
      `clone`(fork)/`execve`
- [x] `getcwd` 17 / `chdir` 49 / `fchdir` 50 — per-process cwd in `FdTable`
- [x] `getdents64` 61 — `vfs.list_dir`, real `d_type`
- [x] `fstat` 80 / `newfstatat` 79 — `abi.KStat` from `Ext2Stat`
- [x] `brk` 214 — grow real, shrink no-op (see the mmap note)

---

## RESOLVED: the layout-sensitive "heisenbug"

All session, growing any module by a bit made an *unrelated* test crash
(Zig panic / EL0 SIGSEGV / pmm leak), and the crash *moved* when debug
prints were added. Two independent causes, both now fixed:

### Cause 1 — per-segment mapping overlap (`f254d7b`)

`loadElf` allocated + mapped each PT_LOAD segment separately, rounding
`[p_vaddr, p_vaddr+p_memsz)` out to 4 KiB pages. lld only guarantees
`p_vaddr ≡ p_offset (mod p_align)` (64 KiB), so two segments routinely
share a 4 KiB page; the second segment's `mapMemory` then repointed the
shared page's PTE at its own fresh frame and dropped the first segment's
bytes. Fixed: one contiguous physical region for the whole
`[target, target+span)`, mapped once. `computeSegmentMapping` deleted.

### Cause 2 — TCB table copied onto the syscall stack (`e223534`)

`process.count()` and `list()` did `for (s_table) |t|` — iterating the
64-entry `[64]Tcb` table (~48 KiB) **by value**, so every call spilled a
48 KiB copy onto the caller's stack as a loop temp. `list()` is called
from `sysWait4` on an 8-page (32 KiB) kernel stack, so the copy
overflowed the stack into whatever the pmm had placed physically below
it. For `/forktest` that was the forking parent's own L1 page table:

```
[SVC] ELR 0x1000010410                       ← /forktest calls clone()
Exception return → EL0 PC 0x10000102e4       ← parent resumes
[Prefetch Abort] ESR 0x82000005 (IFSC 0x5 = translation fault, LEVEL 1)
  FAR 0x10000102e4                            ← parent can't fetch its own code
```

Found with a **gdb hardware watchpoint** on the parent's `L1[64]` entry
(via `-S -gdb tcp::1234`, `add-symbol-file <ko> -o <load addr>` computed
from `elfSpan` + `kernel.ini` order):

```
memcpyFast(dest=<kstack>, src=main.s_table, len=48128)
  ← main.list  (process/main.zig:326)
  ← main.sysWait4  (elf_loader/main.zig:658)
```

Fix: `for (&s_table) |*t|` (by pointer). Same class fixed in
`syscall.count()`, `timer`, `gic_v3`, `fd.testOpenFileCount()`.
Regression test `process:list_does_not_copy_table_to_stack` drives the
functions on a sentinel-painted 256 KiB stack and asserts < 4 KiB used.
The `PAGE_MASK` vs `PTE_ADDR_MASK` sloppiness in the mmu free paths was
audited and is currently harmless (nothing sets table-descriptor bits
48-63), but worth tidying.

---

## Kernel prerequisites (non-syscall — nothing runs without these)

- [x] **FP/SIMD** — `CPACR_EL1.FPEN=0b11` in `exceptions.initCore()`;
      `TaskContext` carries `q0..q31` + `fpsr`/`fpcr` (16-aligned `v`
      field), saved/restored in `ctxsw_switch_to`/`ctxsw_jump_to`;
      `initForkedContext` snapshots the parent's live FP file.
      Exception entry/exit does NOT touch FP — every task switch goes
      through `switch_to`, and the vector path only ever `eret`s back to
      the task it interrupted (no tick preemption yet). Revisit when
      preemptive scheduling lands. Tests: `fp_regs_preserved_across_switch`.
- [x] **`TPIDR_EL0` in `TaskContext`** — `tpidr` field, saved/restored in
      `switch_to`/`jump_to`; `initForkedContext` captures the parent's.
      Tests: `tpidr_preserved_across_switch`,
      `forked_context_captures_tls_and_fp`.
- [x] **auxv** in the elf_loader initial stack — `buildUserStack()` lays
      the full SysV vector (argc/argv/NULL/envp/NULL/auxv/AT_NULL/strings/
      rand16), written through the HHDM alias so it is TTBR0-independent;
      shared by `load` and `execve`. Emits `AT_PHDR/PHENT/PHNUM`
      (PT_PHDR or `lo + e_phoff`), `AT_PAGESZ=4096`, `AT_ENTRY`,
      `AT_RANDOM` (xorshift, not real entropy yet), `AT_CLKTCK=100`,
      `AT_HWCAP=0`, `AT_UID/EUID/GID/EGID=0`, `AT_SECURE=0`, `AT_EXECFN`.
      User stack bumped 16 → 128 pages (512 KiB). Test: `auxv_present`
      (`userland/auxvtest.c` walks its own stack). Skipped
      `AT_SYSINFO_EHDR` (no vDSO).
- [x] **`mmap` address space** — anon `mmap`/`munmap`/`mprotect` +
      `brk` in `elf_loader` (per-pid `s_mm` side table, eager: every page
      a real zeroed frame). `mmap` region at 80 TiB (its own L0 slot so
      `mmu.unmap` prunes the whole L1/L2/L3 chain cleanly); `brk` arena
      at 72 TiB. `mmForget(pid, ..)` on the 3 teardown paths (unload /
      reapChild / execve). Test: `mmap_brk_end_to_end` + `userland/mmtest.c`.
      TWO landmines found the hard way:
        1. **`s_mm` MUST be `.bss` (all-zero defaults).** A `MmState`
           array with non-zero field defaults (`mmap_top`/`brk_cur`)
           lands in `.data`; a ~13 KB+ `.data` section in `elf_loader`
           (the last-loaded module) is loaded/relocated *wrong* by the
           bootloader module loader and corrupts nearby globals
           (execve/fork leak or fault). Kept zero-default, seeded in
           `mmStateLocked`. The module-loader `.data` bug itself is
           unfixed — avoid large non-zero module globals.
        2. **`brk` shrink is a no-op** (pages stay mapped till teardown).
           `mmu.unmap` on the shrink path -> `pruneIfEmpty` frees L3+L2+L1
           and clears the L0 entry; in the full boot this corrupts
           pmm/mmu state (a full `tlbi vmalle1is` did NOT fix it, so it's
           a data-structure issue in the pruneIfEmpty/buddy interaction,
           not TLB). `sysMunmap`'s identical prune of a *different* L0
           slot is fine, so it's order/timing-specific. mallocng uses
           mmap not brk, so a no-op shrink costs nothing.
      Gaps: partial munmap only frees a whole region (addr == base); no
      file-backed mmap; MAP_FIXED unchecked.
- [ ] **Per-process cwd** string in the PCB + real dirfd resolution.
      Today `AT_FDCWD` / any relative path resolves against `/` (the fd
      module prepends `/`). Enough for absolute-path tools; `cd` needs
      the real thing.
- [x] **`struct kstat`** — `abi.KStat` (128-byte aarch64 layout),
      filled from `Ext2Stat` by `fd`'s `fillKStat`.
- [x] **`O_CLOEXEC`/`FD_CLOEXEC`** honored across `execve` (`9a30f53`):
      `FdTable.cloexec` bitmask; openat/pipe2/dup3/`F_DUPFD_CLOEXEC` set it,
      `F_GETFD`/`F_SETFD` read/write it, plain dup clears it, fork copies
      it, `Fd.on_execve` closes flagged fds from `sysExecve`.
      `O_TRUNC` in `openat` truncates a writable regular file to 0;
      `O_NONBLOCK` works for pipes.

---

## Tier A — static hello world starts and prints  — DONE

- [x] `writev` 66 / `readv` 65 — `fd` module, iovec loop over `fdWrite`/`fdRead`
- [x] `set_tid_address` 96 — returns pid
- [x] `ioctl` 29 — `TCGETS`/`TCSETS*`/`TIOCGWINSZ` (80x24)/`TIOCGPGRP` on a
      tty fd; `ENOTTY` otherwise

---

## Tier B — malloc, real coreutils, non-interactive shell

### Memory  — DONE (see the mmap prereq above)
- [x] `mmap` 222 / `munmap` 215 / `mprotect` 226 / `madvise` 233 (→0) /
      `mremap` 216 (→ENOSYS) / `brk` 214 (grow real, shrink no-op)

### Time  — DONE (monotonic only)
- [x] `clock_gettime` 113 / `gettimeofday` 169 / `clock_getres` 114 —
      from `cntvct_el0`/`cntfrq_el0`, CLOCK_MONOTONIC semantics only
      (no wall clock)
- [x] `nanosleep` 101 / `clock_nanosleep` 115 (`e5ec6ed`) — spin to a
      `cntvct_el0` deadline, `EINTR` (+ remainder) on a pending signal.
      `TIMER_ABSTIME` honoured. No sleep queue: busy-waits, so it burns a
      core for the sleep when it is the only runnable task. Test
      `loader:nanosleep_waits` (/sleeptest).

### Signals  — accepted, not delivered (Layer 3)
- [~] `rt_sigaction` 134 / `rt_sigprocmask` 135 / `kill` 129 / `tkill` 130
      / `tgkill` 131 / `set_robust_list` 99 — all accepted → 0, no effect.
      `abort()` will not actually raise yet.

### Identity / info  — DONE
- [x] `getuid`/`geteuid`/`getgid`/`getegid` → 0, `gettid` 178 → pid,
      `uname` 160 (Linux/aarch64/arm-os), `umask` 166 (per-module, one
      global), `prctl` 167 → 0, `sysinfo` 179 / `prlimit64` 261 (canned,
      8 MiB stack / RLIM_INFINITY), `sched_getaffinity` 123 → 1 CPU,
      `getrandom` 278 (xorshift fill)

### Filesystem  — DONE (`fd` module, wrapping `vfs.*`)
- [x] `newfstatat` 79 (+ `AT_EMPTY_PATH`, `AT_SYMLINK_NOFOLLOW`) /
      `fstat` 80 (tty → canned char-dev stat) / `getdents64` 61
      (`vfs.list_dir`, cursor in `OpenFile.offset`, real `d_type`) /
      `faccessat` 48 / `faccessat2` 439 / `mkdirat` 34 / `unlinkat` 35
      (+ `AT_REMOVEDIR`) / `renameat` 38 / `symlinkat` 36 /
      `readlinkat` 78 / `ftruncate` 46 (new `vfs.truncate` → `ext2.file_truncate`)
- [x] `fsync` 82 / `fchmodat` 53 / `fchownat` 54 / `utimensat` 88 → 0
- [x] `getcwd` 17 / `chdir` 49 / `fchdir` 50 — per-process cwd string in
      `FdTable` (copied on fork, "/" on `open_defaults`); relative paths
      and `AT_FDCWD` join onto it, VFS resolves `.`/`..`
- [x] `statfs` 43 / `fstatfs` 44 (canned: ext2 magic, 4 KiB blocks,
      fake counts so `df` doesn't divide by zero) / `mknodat` 33
      (→ `vfs.mknod`, file-type from `mode & S_IFMT`)
- [ ] `statx` 291 — left unregistered (→ ENOSYS → musl falls back)

### Fds / pipes / process
- [x] `fcntl` 25 (`F_DUPFD(_CLOEXEC)`, `F_GET/SETFD` no-op, `F_GET/SETFL`)
- [x] `dup3` 24
- [x] `ppoll` 73 (`700451b`) / `pselect6` 72 (`14d8330`) — real pipe /
      tty / regular-file readiness (`readyMask`), blocking with a cntvct
      deadline (`{0,0}` = non-blocking), `-ERESTARTSYS`/`EINTR` on a signal
- [x] `setpgid` 154 / `getpgid` 155 / `getsid` 156 / `setsid` 157
      (`143eaab`) — real pgid/sid in the Tcb, inherited on fork
- [~] `futex` 98 → 0 (single-thread; musl static locks are uncontended)
- [x] `pipe2` 59 — real in-kernel `Pipe` (4 KiB ring, reader/writer
      refcounts, cooperative block/wake via `scheduler`, `O_NONBLOCK` →
      `EAGAIN`, EOF when writers hit 0, `EPIPE` when readers do).
      `dup3` shares an end (refcount); `fork` inherits both.
- [x] `wait4` `WNOHANG` → returns 0 when no child is reapable
- [ ] `waitid` 95

### `/dev` nodes  — path-recognised in `fd`, no devfs
- [x] `/dev/null` (r→EOF, w→discard), `/dev/zero` (r→zeros),
      `/dev/full` (w→ENOSPC), `/dev/urandom`/`/dev/random` (r→xorshift),
      `/dev/tty`/`/dev/console`/`/dev/std{in,out,err}` → tty backing
- [ ] mknod-backed real `/dev` tree

Coverage: `systest.c`/`syscall_batch` (uname, getrandom, clock_gettime,
writev, mkdirat, openat+O_CREAT, write, ftruncate, fstat, getdents64,
unlinkat, getuid) and `pipetest.c`/`pipe_cwd_dev` (pipe2 rw + EOF, cwd
getcwd/chdir + relative openat, /dev/null/zero/urandom) — both end to
end against the ext2 rootfs. qemu-test `passed=390`.

---

## Tier C — configure scripts / polish (mostly `-ENOSYS`-tolerant)

- [x] `personality` 92, `getrusage` 165 (zeroed), `times` 153,
      `getpriority`/`setpriority` 141/140, `sched_{get,set}scheduler`
      119/120, `sched_getparam` 121, `sched_get_priority_{max,min}`
      125/126, `membarrier` 283, `fadvise64` 223, `getcpu` 168,
      `getgroups` 158 — all accepted / canned (safe: "success, no state"
      is correct on a single-CPU box)
- [ ] `statx` 291 — left **unregistered** on purpose (→ ENOSYS → musl
      falls back to `newfstatat`; a no-op stub would hand back a garbage
      stat). Same reasoning keeps `waitid` 95 unregistered.
- [x] `pselect6` 72 / `ppoll` 73 — real (see "Fds / pipes / process")
- [ ] `epoll_create1`/`ctl`/`pwait` 20-22, `eventfd2` 19, `timerfd_*`
      85-87 — need real kernel event objects; nothing in the target set
      (busybox / coreutils / shells) uses them
- socket family (198+) — skip unless networking

---

## Layer 3 — signals  — CORE DONE (`hnsorens.sys.signal`)

- [x] Signal delivery + `rt_sigreturn` 139 — full AArch64 `rt_sigframe`
      (siginfo + ucontext{ uc_sigmask, sigcontext{ x0..x30, sp, pc,
      pstate }, fpsimd_context }) on the user stack; delivered via a new
      `exceptions.set_user_return_hook` run on every EL0 return.
- [x] `rt_sigaction` 134 (real handler/flags/restorer/mask),
      `rt_sigprocmask` 135, `rt_sigpending` 136
- [x] Sources: `SIGCHLD` on child exit + child stop; `SIGSEGV` from the
      EL0 fault handler (`.sync_data_abort`/`.sync_instruction_abort` →
      raise + `.handled` so the kernel doesn't halt; EL1 faults still
      halt); `SIGINT`/`SIGQUIT`/`SIGTSTP` from the tty on `^C`/`^\`/`^Z`
      to the **foreground process group** (`signal.raise_group`), falling
      back to the running process if no group claimed the tty;
      `kill`/`tkill`/`tgkill`, and `kill(-pgid)` / `kill(0)`.
- [x] Default actions: fatal → `exit(sig)`; ignore CHLD/CONT/WINCH/URG;
      **stop** for STOP/TSTP/TTIN/TTOU (`sched.stop()` / SIGCONT resumes).
      fork inherits dispositions + blocked mask; execve resets.
- [~] `sigaltstack` 132 (`bbee50f`) — per-process `stack_t` stored /
      returned; delivery never switches stacks (SA_ONSTACK ignored)
- [x] `rt_sigsuspend` 133 (`879e734`) — suspend mask spin; pre-suspend
      mask restored via the delivered frame's uc_sigmask
- [ ] `rt_sigtimedwait` 137, `rt_sigqueueinfo` 138
- [x] `SIGPIPE` on write to a broken pipe (`pipeWrite` raises it before
      returning `EPIPE`; default action terminates the writer)
- [x] `SA_RESTART` (`b41adb0`) — a blocking syscall returns internal
      `-ERESTARTSYS`; `deliver()` rewinds the `svc` (restoring x0 via
      `signal.mark_restart`) for an SA_RESTART handler or a spurious
      wake, else rewrites x0 → `-EINTR`. Wired for `ppoll`/`pselect6`/
      `wait4`; `nanosleep` keeps `EINTR` + remainder by design.

## Layer 3 — job control  — DONE

1. **pgid / sid** (`143eaab`) — `Tcb.pgid`/`sid` (`= pid` at create,
   inherited on fork, `setsid` starts a new session). `abi.Process`
   `set_pgid`/`get_pgid`/`set_sid`/`get_sid`; `ProcessInfo` carries them.
   `kill()` does POSIX `pid <= 0` group semantics via `signal.raise_group`.
2. **termios** (`8723f40`) — tty owns a real 36-byte kernel `struct
   termios`; `TCGETS`/`TCSETS(W/F)` copy it in/out and re-derive
   canonical/echo from `c_lflag` (ICANON/ECHO). Raw mode for readline.
3. **foreground pgrp** (`8723f40`) — tty holds a fg pgid; `TIOCGPGRP` /
   `TIOCSPGRP` (`tcgetpgrp`/`tcsetpgrp`) / `TIOCSCTTY`. `^C`/`^\`/`^Z`
   signal it.
4. **stop / continue** (`f5a22f4`) — `ProcessState.stopped` +
   `Scheduler.stop()`; `wake()` also resumes `.stopped`. `deliver()`
   parks on a stop signal and raises SIGCHLD; SIGCONT clears + wakes.
   `wait4(WUNTRACED)` reports a stopped child (`W_STOPPED | SIGSTOP<<8`).
   Tests: `loader:pgid_sid`, `tty:termios_drives_mode`,
   `loader:job_control_stop_cont`.

Not yet: `SIGTTIN`/`SIGTTOU` when a background pgrp touches the tty (the
tty read/write path doesn't check the caller's pgid vs the fg pgid yet);
`wait4` `WCONTINUED`.

---

## Suggested first batch (→ static busybox `echo`/`cat`/`ls`/`mkdir`/`rm`)

1. Prereqs: FP/SIMD, `TPIDR_EL0` in `TaskContext`, auxv (`AT_PAGESZ` +
   `AT_RANDOM` minimum), `mmap` bump region
2. `writev`, `readv`, `set_tid_address`, `ioctl` (tty)
3. `mmap`, `munmap`, `mprotect`, `madvise`, `brk`
4. `fstat`, `newfstatat`, `getdents64` + `struct kstat`
5. `fcntl`, `dup3`, `faccessat2`
6. `mkdirat`, `unlinkat`, `renameat`, `readlinkat`, `ftruncate`
7. `clock_gettime`, `nanosleep`, `getrandom`, `uname`, `getuid`-family,
   `rt_sigprocmask`, `rt_sigaction` (record-only)
