# Static musl userland — syscall & kernel work checklist

Target: **statically-linked musl** (`zig cc -target aarch64-linux-musl -lc`,
no dynamic loader). Enough of the Linux/aarch64 syscall ABI + kernel
plumbing to run static busybox, then GNU coreutils / `bash -c`, then
interactive bash.

Numbers are the `asm-generic/unistd.h` (aarch64) ones — the same table
`shared/abi_types.zig` already uses.

Legend: `[x]` done · `[~]` partial / stubbed · `[ ]` not started

---

## Already working (registered)

- [x] `read` 63, `write` 64, `openat` 56, `close` 57, `lseek` 62, `dup` 23
- [x] `getpid` 172, `getppid` 173, `sched_yield` 124
- [x] `exit` 93, `exit_group` 94, `wait4` 260
- [x] `clone` 220 (== fork), `execve` 221

## Numbered in abi_types.zig but NOT wired

- [ ] `getcwd` 17        — needs per-process cwd
- [ ] `chdir` 49         — needs per-process cwd
- [ ] `getdents64` 61    — wrap `vfs.list_dir`
- [ ] `fstat` 80         — wrap `vfs.stat` + `struct kstat`
- [ ] `brk` 214          — stub: return current break

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
- [ ] `statfs` 43 / `fstatfs` 44 / `mknodat` 33 / `statx` 291
      (musl falls back to `newfstatat` on ENOSYS, fine)

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

- [ ] `pselect6` 72, `epoll_create1` 20, `epoll_ctl` 21, `epoll_pwait` 22
- [ ] `eventfd2` 19, `timerfd_create` 85, `timerfd_settime` 86, `timerfd_gettime` 87
- [ ] `futex` 98            — stub-ish while single-thread
- [ ] `personality` 92, `getrusage` 165, `times` 153
- [ ] `getpriority` 141, `setpriority` 140
- [ ] `sched_get_priority_max` 125, `sched_get_priority_min` 126
- [ ] `membarrier` 283, `fadvise64` 223, `getcpu` 168
- [ ] `statx` 291           — `-ENOSYS` → musl falls back to `newfstatat`
- socket family (198+) — skip unless networking

---

## Layer 3 — interactive bash (real signals subsystem, later)

- [ ] Signal delivery + `rt_sigreturn` 139 — build `rt_sigframe` on the
      user/alt stack, set `ELR`/`x0..x2`/`lr`, restore on return
- [ ] `sigaltstack` 132, `rt_sigpending` 136, `rt_sigsuspend` 133,
      `rt_sigtimedwait` 137, `rt_sigqueueinfo` 138
- [ ] Signal sources: `SIGCHLD` on child exit; `SIGSEGV/BUS/ILL` from the
      fault handlers; `SIGINT/QUIT` from the tty; `SIGPIPE`; `SIGWINCH`
- [ ] termios in the tty — `TCSETS/TCSETSW/TCSETSF` driving `set_mode`
- [ ] job control — foreground pgrp, `TIOCSPGRP/TIOCGPGRP`, `TIOCSCTTY`,
      `SIGTSTP/CONT/TTIN/TTOU`, `tcsetpgrp`

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
