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
- [ ] **auxv** in the elf_loader initial stack (today only `AT_NULL`):
  - [ ] `AT_PAGESZ = 4096` — **mandatory**, no musl fallback
  - [ ] `AT_RANDOM` — 16 bytes, stack canary seed
  - [ ] `AT_PHDR` / `AT_PHENT` / `AT_PHNUM` — needed for real `__thread`
  - [ ] `AT_CLKTCK` 100, `AT_HWCAP`, `AT_UID/GID/EUID/EGID`,
        `AT_SECURE=0`, `AT_EXECFN`, `AT_ENTRY`
  - skip `AT_SYSINFO_EHDR` (no vDSO — musl uses direct syscalls)
- [ ] **`mmap` address space** — demand-zero anonymous pages in user
      TTBR0; per-process VMA list (start with a bump region ~96 GiB).
- [ ] **Per-process cwd** string in the PCB + `AT_FDCWD` / dirfd
      relative-path resolution in the vfs (today absolute-only).
- [ ] **`struct kstat`** (aarch64 layout) translation from `Ext2Stat`.
- [ ] **`O_CLOEXEC`/`FD_CLOEXEC`** honored across `execve`;
      `O_TRUNC`/`O_DIRECTORY`/`O_NONBLOCK`/`O_APPEND` in `openat`.

---

## Tier A — static hello world starts and prints

- [ ] `writev` 66            — loop over `fd.write`; **no stdio output without it**
- [ ] `set_tid_address` 96   — stub, store & return fake tid (= pid)
- [~] `ioctl` 29             — `TCGETS` + `TIOCGWINSZ` on the tty; `ENOTTY` for files

---

## Tier B — malloc, real coreutils, non-interactive shell

### Memory
- [ ] `mmap` 222       — `MAP_ANONYMOUS|MAP_PRIVATE`, `MAP_FIXED`, demand-zero
- [ ] `munmap` 215
- [ ] `mprotect` 226
- [ ] `madvise` 233    — stub → 0
- [ ] `mremap` 216     — may `-ENOSYS` (realloc falls back)
- [ ] `brk` 214        — stub → current break

### Time
- [ ] `clock_gettime` 113
- [ ] `gettimeofday` 169
- [ ] `nanosleep` 101
- [ ] `clock_nanosleep` 115
- [ ] `clock_getres` 114        — canned
- [ ] `getrandom` 278           — stub-fill for now

### Signals (accept + record; real delivery = Layer 3)
- [ ] `rt_sigaction` 134
- [ ] `rt_sigprocmask` 135
- [ ] `kill` 129
- [ ] `tgkill` 131             — `abort()` path
- [ ] `set_robust_list` 99     — stub → 0

### Identity / info (stub-able)
- [ ] `getuid` 174, `geteuid` 175, `getgid` 176, `getegid` 177  → 0
- [ ] `gettid` 178            → pid
- [ ] `uname` 160            — fill struct
- [ ] `getgroups` 158        → 0
- [ ] `umask` 166            — per-process umask
- [ ] `prctl` 167           — stub → 0
- [ ] `sysinfo` 179, `prlimit64` 261, `getrlimit` 163  — canned
- [ ] `sched_getaffinity` 123  → 1-CPU mask

### Filesystem (wrap existing `vfs.*` — ext2 write path already exists)
- [ ] `newfstatat` 79   — `vfs.stat`/`vfs.lstat` → `struct kstat`
- [ ] `fstat` 80        — via fd
- [ ] `getdents64` 61   — `vfs.list_dir`
- [ ] `getcwd` 17       — per-process cwd
- [ ] `chdir` 49, `fchdir` 50
- [ ] `faccessat` 48, `faccessat2` 439
- [ ] `mkdirat` 34      — `vfs.mkdir`
- [ ] `unlinkat` 35     — `vfs.remove` (+ `AT_REMOVEDIR`)
- [ ] `renameat` 38, `renameat2` 276  — `vfs.rename`
- [ ] `symlinkat` 36    — `vfs.symlink`
- [ ] `readlinkat` 78   — `vfs.readlink`
- [ ] `truncate` 45, `ftruncate` 46   — `ext2.fileTruncate`
- [ ] `mknodat` 33      — `vfs.mknod` (for `/dev`)
- [ ] `fchmodat` 53     — real-ish; `fchownat` 54, `utimensat` 88 → stub 0
- [ ] `fsync` 82, `fdatasync` 83, `sync` 81  — stub → 0 / flush
- [ ] `statfs` 43, `fstatfs` 44   — canned

### Fds / pipes / process
- [ ] `fcntl` 25       — `F_DUPFD(_CLOEXEC)`, `F_GETFD/SETFD`, `F_GETFL/SETFL`
- [ ] `dup3` 24        — musl `dup2`; redirection
- [ ] `pipe2` 59       — new in-kernel pipe object in `fd`
- [ ] `readv` 65       — mirror `writev`
- [ ] `ppoll` 73       — musl `poll`; start: ready fds return immediately
- [ ] `wait4` 260      — extend with `WNOHANG`
- [ ] `waitid` 95      — route to wait4 logic
- [ ] `setpgid` 154, `getpgid` 155, `setsid` 157, `getsid` 156  — accept

### `/dev` nodes (via `mknodat` into rootfs or a devfs shim)
- [ ] `/dev/null`, `/dev/zero`, `/dev/tty`, `/dev/console`, `/dev/urandom`

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
