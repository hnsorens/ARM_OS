# Static musl userland — syscall & kernel work checklist

Target: **statically-linked musl** (`zig cc -target aarch64-linux-musl -lc`,
no dynamic loader). Enough of the Linux/aarch64 syscall ABI + kernel
plumbing to run static busybox, then GNU coreutils / `bash -c`, then
interactive bash.

Numbers are the `asm-generic/unistd.h` (aarch64) ones — the same table
`shared/abi_types.zig` already uses.

Legend: `[x]` done · `[~]` partial / stubbed · `[ ]` not started

## STATUS (qemu-test passed=392)

Done: FP/SIMD + TPIDR_EL0 + auxv (Layer 0); anon mmap/munmap/mprotect +
brk; the full coreutils syscall surface (stat family, getdents64, fcntl,
ioctl, the `*at` family, writev/readv, time, id stubs, Tier C stubs);
pipe2 + per-process cwd + `/dev` nodes; statfs/fstatfs/mknodat; and
**Layer 3 core signals** (rt_sigaction/procmask/pending/return, delivery
via the exceptions return-to-EL0 hook, kill family, SIGSEGV-from-fault,
SIGCHLD, SIGPIPE, `^C`→SIGINT, default actions, fork inherit).

Not done: real `nanosleep` (returns 0 now — no sleep queue); `O_CLOEXEC`
across execve; job control + termios driving the tty; `rt_sigsuspend` /
`sigaltstack` / SA_RESTART; event objects (epoll/timerfd/eventfd/
pselect6); `statx`/`waitid` deliberately unregistered so musl falls back.

~100 syscalls registered across `fd`, `elf_loader`, `signal`.

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
- [ ] **`O_CLOEXEC`/`FD_CLOEXEC`** honored across `execve`;
      `O_TRUNC`/`O_NONBLOCK` in `openat` (`O_CREAT`/`O_APPEND`/`O_DIRECTORY`
      already work; `fcntl` accepts `F_*` but does not track CLOEXEC).

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
- [~] `nanosleep` 101 / `clock_nanosleep` 115 — **return 0 immediately**
      (no sleep queue yet; `sleep` is instant). TODO: real timer sleep.

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
- [~] `ppoll` 73 → 0 (treated as "nothing ready / timed out")
- [x] `setpgid` 154 → 0, `getpgid`/`getsid`/`setsid` → pid
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
- [ ] `pselect6` 72, `epoll_create1`/`ctl`/`pwait` 20-22, `eventfd2` 19,
      `timerfd_*` 85-87 — need real event objects; most tools don't
- socket family (198+) — skip unless networking

---

## Layer 3 — signals  — CORE DONE (`hnsorens.sys.signal`)

- [x] Signal delivery + `rt_sigreturn` 139 — full AArch64 `rt_sigframe`
      (siginfo + ucontext{ uc_sigmask, sigcontext{ x0..x30, sp, pc,
      pstate }, fpsimd_context }) on the user stack; delivered via a new
      `exceptions.set_user_return_hook` run on every EL0 return.
- [x] `rt_sigaction` 134 (real handler/flags/restorer/mask),
      `rt_sigprocmask` 135, `rt_sigpending` 136
- [x] Sources: `SIGCHLD` on child exit; `SIGSEGV` from the EL0 fault
      handler (`.sync_data_abort`/`.sync_instruction_abort` → raise +
      `.handled` so the kernel doesn't halt; EL1 faults still halt);
      `SIGINT`/`SIGQUIT` from the tty on `^C`/`^\` (no fg pgrp yet →
      signals the running process); `kill`/`tkill`/`tgkill`
- [x] Default actions: fatal → `exit(sig)`; ignore CHLD/CONT/WINCH/URG/
      STOP/TSTP. fork inherits dispositions + blocked mask; execve resets.
- [~] `sigaltstack` 132 — not implemented (SA_ONSTACK ignored)
- [ ] `rt_sigsuspend` 133, `rt_sigtimedwait` 137, `rt_sigqueueinfo` 138
- [x] `SIGPIPE` on write to a broken pipe (`pipeWrite` raises it before
      returning `EPIPE`; default action terminates the writer)
- [ ] no `SA_RESTART` — a signal that wakes a blocked syscall lets it
      return early (EINTR/short) rather than restarting

## Layer 3 — interactive bash (job control, later)

- [ ] termios in the tty — `TCSETS/TCSETSW/TCSETSF` actually driving
      `set_mode` (readline raw/canonical); `ioctl` currently accepts them
      and no-ops
- [ ] job control — foreground pgrp, `TIOCSPGRP/TIOCGPGRP`, `TIOCSCTTY`,
      `SIGTSTP/CONT/TTIN/TTOU`, `tcsetpgrp`. `^C` currently signals the
      running process, not the fg pgrp.

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
